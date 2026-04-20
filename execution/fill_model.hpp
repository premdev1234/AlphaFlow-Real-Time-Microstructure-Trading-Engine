// fill_model.hpp
#pragma once
// ============================================================
//  fill_model.hpp  –  HFT Execution Module
//  Realistic fill simulation:
//    MARKET orders → immediate at best available price
//    LIMIT  orders → queue-gated, fill only when at front
//
//  Supports partial fills across multiple market updates.
//  No heap allocation; integrates with QueueModel by reference.
// ============================================================

#include "order.hpp"
#include "queue_model.hpp"

#include <cstdint>
#include <array>
#include <algorithm>
#include <cmath>

namespace hft {

// ─── MarketTick: one snapshot of market activity ────────────
struct MarketTick {
    double   best_bid;
    double   best_ask;
    int      bid_depth;        // total liquidity on bid side
    int      ask_depth;        // total liquidity on ask side
    int      trade_volume;     // contracts that traded this tick
    double   trade_price;      // price at which trades occurred
    int64_t  timestamp_ns;
};

// ─── FillResult: outcome of one fill attempt ────────────────
struct FillResult {
    int    filled_qty   = 0;
    double fill_price   = 0.0;
    bool   partial      = false;
    bool   fully_filled = false;
};

// ─── FillModel ───────────────────────────────────────────────
//  Stateless fill engine – all state lives in Order/QueueModel.
//  Call process_market_order or process_limit_order each tick.
struct FillModel {

    // ── Slippage settings ────────────────────────────────────
    double  tick_size        = 0.01;   // minimum price increment
    double  slippage_factor  = 0.0002; // fraction of spread added per 1000 units

    FillModel() = default;
    explicit FillModel(double tick, double slip)
        : tick_size(tick), slippage_factor(slip) {}

    // ────────────────────────────────────────────────────────
    //  MARKET ORDER fill
    //  Fills immediately at best_ask (BUY) or best_bid (SELL).
    //  Slippage applied when order > 10% of available depth.
    // ────────────────────────────────────────────────────────
    FillResult process_market_order(Order&           order,
                                    const MarketTick& tick) const noexcept
    {
        FillResult res;
        if (order.is_filled() || order.is_terminal()) return res;

        // Choose aggressor price
        double base_price = (order.side == Side::BUY)
                            ? tick.best_ask
                            : tick.best_bid;

        // Available liquidity on the side we're hitting
        int available = (order.side == Side::BUY)
                        ? tick.ask_depth
                        : tick.bid_depth;

        if (available <= 0) return res;   // no liquidity

        // How much we can fill this tick
        int fill_qty = std::min(order.remaining_qty, available);

        // Slippage: larger orders eat into the book
        double slip = compute_slippage(fill_qty, available, order.side);
        double fill_price = base_price + slip;

        // Snap to tick grid
        fill_price = snap_to_tick(fill_price);

        order.fill(fill_qty, fill_price, tick.timestamp_ns);

        res.filled_qty   = fill_qty;
        res.fill_price   = fill_price;
        res.partial      = !order.is_filled();
        res.fully_filled = order.is_filled();
        return res;
    }

    // ────────────────────────────────────────────────────────
    //  LIMIT ORDER fill
    //  Requires queue position to be cleared first.
    //  Fills at the order's limit price (price improvement possible).
    // ────────────────────────────────────────────────────────
    FillResult process_limit_order(Order&           order,
                                   QueueModel&       queue,
                                   const MarketTick& tick) const noexcept
    {
        FillResult res;
        if (order.is_filled() || order.is_terminal()) return res;

        // Advance queue position
        int queue_fill = queue.update(tick.trade_volume);

        if (queue_fill <= 0) return res;   // still waiting in queue

        // Determine actual fill price
        // Price improvement: if market traded better than our limit, we get it
        double limit_px = order.price;
        double fill_price = compute_limit_fill_price(order.side,
                                                     limit_px,
                                                     tick.trade_price,
                                                     tick.best_bid,
                                                     tick.best_ask);
        fill_price = snap_to_tick(fill_price);

        // Only fill if our limit price is still marketable
        if (!is_marketable(order.side, limit_px, tick.best_bid, tick.best_ask))
            return res;

        int fill_qty = std::min(queue_fill, order.remaining_qty);
        order.fill(fill_qty, fill_price, tick.timestamp_ns);

        res.filled_qty   = fill_qty;
        res.fill_price   = fill_price;
        res.partial      = !order.is_filled();
        res.fully_filled = order.is_filled();
        return res;
    }

    // ────────────────────────────────────────────────────────
    //  IOC (Immediate-Or-Cancel) variant
    //  Fill what's available now, cancel the rest
    // ────────────────────────────────────────────────────────
    FillResult process_ioc_order(Order&           order,
                                 const MarketTick& tick) const noexcept
    {
        FillResult res = process_market_order(order, tick);
        // Cancel remainder
        if (!order.is_filled()) {
            order.status = OrderStatus::CANCELLED;
        }
        return res;
    }

private:
    // Slippage model: linear in size / available_depth
    inline double compute_slippage(int fill_qty,
                                   int available,
                                   Side side) const noexcept
    {
        if (available <= 0) return 0.0;
        double ratio = static_cast<double>(fill_qty) / available;
        double raw_slip = slippage_factor * ratio * fill_qty / 1000.0;
        // BUY pays more, SELL receives less
        return (side == Side::BUY) ? +raw_slip : -raw_slip;
    }

    inline double compute_limit_fill_price(Side   side,
                                           double limit_px,
                                           double trade_px,
                                           double bid,
                                           double ask) const noexcept
    {
        // Price improvement: passive order gets the better of limit or trade
        if (side == Side::BUY) {
            // We were willing to pay limit_px; if traded below, we get trade_px
            return std::min(limit_px, trade_px);
        } else {
            // We were willing to sell at limit_px; if traded above, we get trade_px
            return std::max(limit_px, trade_px);
        }
    }

    inline bool is_marketable(Side side, double limit_px,
                               double bid, double ask) const noexcept
    {
        if (side == Side::BUY)  return limit_px >= ask;
        else                    return limit_px <= bid;
    }

    inline double snap_to_tick(double price) const noexcept {
        if (tick_size <= 0.0) return price;
        return std::round(price / tick_size) * tick_size;
    }
};

} // namespace hft