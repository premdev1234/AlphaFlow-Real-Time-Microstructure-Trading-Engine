// order_book_sim.hpp
#pragma once
// ============================================================
//  order_book_sim.hpp  –  HFT Execution Module
//  Simplified Limit Order Book (LOB) simulator.
//
//  Maintains a compact, cache-friendly representation of
//  the top N levels of the book. Supports:
//    – Best bid/ask query
//    – Liquidity consumption (market orders)
//    – Depth updates (from market data feed)
//    – Queue entry (limit orders placed at a level)
//
//  DESIGN:
//    Fixed-size arrays, sorted by price.
//    No std::map, no heap. N=10 levels covers 99%+ of HFT needs.
// ============================================================

#include <cstdint>
#include <array>
#include <algorithm>
#include <cassert>
#include <cstring>
#include "order.hpp"
#include <cmath>
#include "fill_model.hpp"
namespace hft {

static constexpr int MAX_BOOK_LEVELS = 10;

// ─── Price level in the book ─────────────────────────────────
struct PriceLevel {
    double price    = 0.0;
    int    quantity = 0;    // total available quantity at this level
    int    orders   = 0;    // number of resting orders (for queue model)
};

// ─── Book snapshot (one-sided: bids or asks) ─────────────────
//  Levels stored best-first:
//    bids[0] = best bid (highest)
//    asks[0] = best ask (lowest)
struct BookSide {
    std::array<PriceLevel, MAX_BOOK_LEVELS> levels;
    int depth = 0;   // number of valid levels

    void clear() noexcept {
        depth = 0;
        for (auto& l : levels) { l.price = 0.0; l.quantity = 0; l.orders = 0; }
    }

    // Total liquidity summed across all levels
    inline int total_quantity() const noexcept {
        int total = 0;
        for (int i = 0; i < depth; ++i) total += levels[i].quantity;
        return total;
    }

    inline const PriceLevel& best() const noexcept {
        return levels[0];
    }

    // Consume `qty` starting from best level.
    // Returns the volume-weighted average price consumed.
    inline double consume(int qty) noexcept {
        double notional = 0.0;
        int    consumed = 0;
        for (int i = 0; i < depth && consumed < qty; ++i) {
            int take = std::min<int>(qty - consumed, levels[i].quantity);
            notional          += levels[i].price * take;
            levels[i].quantity -= take;
            consumed           += take;
            // Collapse empty levels
            if (levels[i].quantity == 0) {
                // Shift remaining levels up
                for (int j = i; j < depth - 1; ++j)
                    levels[j] = levels[j + 1];
                --depth;
                --i;  // re-examine this index
            }
        }
        return (consumed > 0) ? notional / consumed : 0.0;
    }

    // Add or update a level (upsert by price)
    inline void upsert(double price, int qty, int orders = 0) noexcept {
        for (int i = 0; i < depth; ++i) {
            if (levels[i].price == price) {
                levels[i].quantity = qty;
                levels[i].orders   = orders;
                return;
            }
        }
        if (depth < MAX_BOOK_LEVELS) {
            levels[depth++] = { price, qty, orders };
        }
        // Keep sorted best-first (caller's responsibility to pass in order,
        // or we sort here — small array so insertion sort is fine)
        // Bids: descending; Asks: ascending — determined by caller flag
    }
};

// ─── OrderBookSim ────────────────────────────────────────────
struct OrderBookSim {

    BookSide bids;   // sorted descending by price
    BookSide asks;   // sorted ascending  by price

    double   tick_size    = 0.01;
    int64_t  last_update_ns = 0;

    OrderBookSim() { bids.clear(); asks.clear(); }

    // ── Initialise with a simple 2-sided book ────────────────
    void init(double mid_price,
              int    qty_per_level = 500,
              double spread        = 0.02,
              double level_step    = 0.01) noexcept
    {
        bids.clear(); asks.clear();
        double half_spread = spread / 2.0;
        for (int i = 0; i < MAX_BOOK_LEVELS; ++i) {
            double bid_px = mid_price - half_spread - i * level_step;
            double ask_px = mid_price + half_spread + i * level_step;
            int decay_qty = qty_per_level / (i + 1);   // thinner farther out
            bids.levels[i] = { bid_px, decay_qty, decay_qty / 10 + 1 };
            asks.levels[i] = { ask_px, decay_qty, decay_qty / 10 + 1 };
        }
        bids.depth = asks.depth = MAX_BOOK_LEVELS;
    }

    // ── Convenience accessors ────────────────────────────────
    inline double best_bid()   const noexcept { return bids.levels[0].price; }
    inline double best_ask()   const noexcept { return asks.levels[0].price; }
    inline double mid_price()  const noexcept { return (best_bid() + best_ask()) * 0.5; }
    inline double spread()     const noexcept { return best_ask() - best_bid(); }

    inline int bid_depth_at_best() const noexcept { return bids.levels[0].quantity; }
    inline int ask_depth_at_best() const noexcept { return asks.levels[0].quantity; }

    // ── Simulate a market order hitting the book ──────────────
    //  Returns VWAP of the fill. Modifies book depth.
    inline double execute_market_order(Side side, int qty) noexcept {
        if (side == Side::BUY)  return asks.consume(qty);
        else                    return bids.consume(qty);
    }

    // ── Queue entry: returns liquidity ahead for a limit order ─
    //  `price` – limit price being placed
    //  `side`  – which side
    inline int liquidity_ahead(double price, Side side) const noexcept {
        const BookSide& book = (side == Side::BUY) ? bids : asks;
        for (int i = 0; i < book.depth; ++i) {
            if (book.levels[i].price == price)
                return book.levels[i].quantity;   // all qty at this level is ahead
        }
        return 0;
    }

    // ── Apply a trade tick (from market data) ────────────────
    //  Trade events consume from book; also generates a
    //  market tick for fill model processing.
    inline MarketTick apply_trade(double trade_price,
                                  int    trade_vol,
                                  int64_t ts_ns) noexcept
    {
        last_update_ns = ts_ns;

        // Determine aggressor side
        Side aggressor = (trade_price >= mid_price()) ? Side::BUY : Side::SELL;
        if (aggressor == Side::BUY)   asks.consume(trade_vol);
        else                          bids.consume(trade_vol);

        return MarketTick{
            best_bid(), best_ask(),
            bids.levels[0].quantity,
            asks.levels[0].quantity,
            trade_vol, trade_price, ts_ns
        };
    }

    // ── Refresh book from external feed ──────────────────────
    void update_level(Side side, double price, int qty, int64_t ts_ns) noexcept {
        last_update_ns = ts_ns;
        if (side == Side::BUY) bids.upsert(price, qty);
        else                   asks.upsert(price, qty);
    }

    // ── Snapshot for display / logging ───────────────────────
    struct Snapshot {
        double best_bid, best_ask, mid, spread;
        int    bid_qty, ask_qty;
        int64_t ts_ns;
    };

    inline Snapshot snapshot() const noexcept {
        return {
            best_bid(), best_ask(), mid_price(), spread(),
            bid_depth_at_best(), ask_depth_at_best(),
            last_update_ns
        };
    }
};

} // namespace hft