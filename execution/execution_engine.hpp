// execution_engine.hpp
#pragma once
// ============================================================
//  execution_engine.hpp  –  HFT Execution Module
//  Core engine: Signal → Order → Fill → PnL
//
//  Responsibilities:
//    1. Signal → order sizing (confidence-scaled)
//    2. Order type selection (limit vs market by urgency/alpha)
//    3. Risk checks (skip if risk too high, position limits)
//    4. Order lifecycle management (live, partial, filled, cancelled)
//    5. Cancellation logic (timeout, adverse price movement)
//    6. Position & PnL tracking
//    7. Market impact estimation before order placement
//
//  PERFORMANCE:
//    – Fixed-size order pool (no heap in hot path)
//    – Inline helpers, no virtual dispatch
//    – All state is POD or fixed-size arrays
// ============================================================

#include "order.hpp"
#include "signal.hpp"
#include "fill_model.hpp"
#include "queue_model.hpp"
#include "impact_model.hpp"
#include "order_book_sim.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <cassert>

namespace hft {

// ─── Engine configuration ────────────────────────────────────
struct EngineConfig {
    // Sizing
    int    base_order_size      = 100;     // units per unit confidence
    int    max_order_size       = 2000;    // hard cap
    int    max_position         = 5000;    // inventory limit (long + short)

    // Risk controls
    double max_risk_threshold   = 0.70;   // skip trade if signal.risk > this
    double min_alpha_threshold  = 0.05;   // skip if |alpha| < this
    double min_confidence       = 0.20;   // skip if confidence < this

    // Order type selection
    double market_order_alpha   = 0.80;   // |alpha| above this → MARKET
    int    market_order_urgency = 2;      // urgency >= this → MARKET

    // Cancellation
    int64_t limit_order_timeout_ns = 5'000'000'000LL; // 5 seconds
    double  adverse_move_ticks     = 3.0;             // cancel if adverse > 3 ticks
    double  tick_size              = 0.01;

    // Pool
    static constexpr int MAX_LIVE_ORDERS = 64;
};

// ─── ExecutionReport (returned per on_signal call) ───────────
struct ExecutionReport {
    uint64_t order_id;
    bool     order_placed;
    bool     rejected;          // risk rejected
    char     reject_reason[32]; // human-readable

    // Fill state at time of report
    int      filled_qty;
    int      total_qty;
    double   avg_fill_price;
    double   estimated_impact;

    // Position & PnL snapshot
    int      position;
    double   realised_pnl;
    double   unrealised_pnl;
    double   total_pnl;

    // Lifecycle
    OrderStatus status;
};

// ─── OrderSlot: pool entry combining Order + QueueModel ───────
struct OrderSlot {
    Order      order;
    QueueModel queue;
    bool       active = false;
};

// ─── ExecutionEngine ─────────────────────────────────────────
struct ExecutionEngine {

    EngineConfig cfg;
    FillModel    fill_model;
    ImpactModel  impact_model;
    OrderBookSim* book = nullptr;   // non-owning reference

    // Fixed-size order pool
    std::array<OrderSlot, EngineConfig::MAX_LIVE_ORDERS> order_pool{};
    uint64_t next_order_id = 1;
    int      live_count    = 0;

    // Position tracking
    int      position      = 0;     // + = long, - = short
    double   realised_pnl  = 0.0;
    double   entry_notional= 0.0;   // notional of current position

    // Statistics
    uint64_t signals_received   = 0;
    uint64_t orders_placed      = 0;
    uint64_t orders_filled      = 0;
    uint64_t orders_cancelled   = 0;
    uint64_t risk_rejections    = 0;

    // ── Initialise ──────────────────────────────────────────
    explicit ExecutionEngine(OrderBookSim* book_ref,
                             EngineConfig  config   = {},
                             FillModel     fm       = {},
                             ImpactModel   im       = {})
        : cfg(config), fill_model(fm), impact_model(im), book(book_ref)
    {}

    // ────────────────────────────────────────────────────────
    //  PRIMARY ENTRY POINT: on_signal
    //  Called by strategy layer each time a new Signal arrives.
    //  Returns an ExecutionReport describing what was done.
    // ────────────────────────────────────────────────────────
    ExecutionReport on_signal(const Signal& sig, int64_t ts_ns) noexcept {
        ++signals_received;

        ExecutionReport rpt{};
        rpt.order_placed    = false;
        rpt.rejected        = false;
        rpt.position        = position;
        rpt.realised_pnl    = realised_pnl;

        // ── 1. Risk / quality pre-checks ────────────────────
        if (!passes_risk_checks(sig, rpt)) {
            ++risk_rejections;
            return rpt;
        }

        // ── 2. Determine side, size, type ───────────────────
        Side  side = sig.is_buy() ? Side::BUY : Side::SELL;
        int   size = compute_order_size(sig);
        OType type = choose_order_type(sig);

        // ── 3. Position limit check ──────────────────────────
        if (!position_allows(side, size)) {
            rpt.rejected = true;
            std::strncpy(rpt.reject_reason, "POSITION_LIMIT", 31);
            ++risk_rejections;
            return rpt;
        }

        // ── 4. Market impact estimation ──────────────────────
        int avail = (side == Side::BUY) ? book->ask_depth_at_best()
                                         : book->bid_depth_at_best();
        double impact = impact_model.estimate_impact(size, avail, side);

        // ── 5. Choose price ──────────────────────────────────
        double limit_price = choose_limit_price(side, type, impact, ts_ns);

        // ── 6. Place order ───────────────────────────────────
        int slot_idx = allocate_slot();
        if (slot_idx < 0) {
            rpt.rejected = true;
            std::strncpy(rpt.reject_reason, "POOL_EXHAUSTED", 31);
            return rpt;
        }

        OrderSlot& slot = order_pool[slot_idx];
        slot.order = Order::make(next_order_id++, 0, side, type,
                                 limit_price, size, ts_ns);
        slot.order.status = OrderStatus::LIVE;

        // Initialise queue model for limit orders
        int ahead  = (type == OType::LIMIT) ? book->liquidity_ahead(limit_price, side) : 0;
        int behind = avail - ahead;
        slot.queue = QueueModel::make(size, ahead, std::max(behind, 0));
        slot.active = true;
        ++live_count;
        ++orders_placed;

        // ── 7. For MARKET orders: fill immediately ───────────
        if (type == OType::MARKET) {
            MarketTick tick = {
                book->best_bid(), book->best_ask(),
                book->bid_depth_at_best(), book->ask_depth_at_best(),
                size, (side==Side::BUY ? book->best_ask() : book->best_bid()),
                ts_ns
            };
            FillResult fr = fill_model.process_market_order(slot.order, tick);
            if (fr.filled_qty > 0) {
                update_position(slot.order, fr.filled_qty, fr.fill_price);
                if (slot.order.is_filled()) {
                    ++orders_filled;
                    slot.active = false;
                    --live_count;
                }
            }
        }

        // ── 8. Build report ──────────────────────────────────
        rpt.order_id          = slot.order.id;
        rpt.order_placed      = true;
        rpt.filled_qty        = slot.order.filled_qty;
        rpt.total_qty         = slot.order.quantity;
        rpt.avg_fill_price    = slot.order.avg_fill_price;
        rpt.estimated_impact  = impact;
        rpt.status            = slot.order.status;
        rpt.position          = position;
        rpt.realised_pnl      = realised_pnl;
        rpt.unrealised_pnl    = compute_unrealised_pnl(book->mid_price());
        rpt.total_pnl         = rpt.realised_pnl + rpt.unrealised_pnl;

        return rpt;
    }

    // ────────────────────────────────────────────────────────
    //  on_market_tick: update all live limit orders each tick
    // ────────────────────────────────────────────────────────
    void on_market_tick(const MarketTick& tick) noexcept {
        for (int i = 0; i < EngineConfig::MAX_LIVE_ORDERS; ++i) {
            OrderSlot& slot = order_pool[i];
            if (!slot.active) continue;
            if (slot.order.type == OType::MARKET) continue;  // already filled

            // Check cancellation conditions
            if (should_cancel(slot.order, tick)) {
                slot.order.status = OrderStatus::CANCELLED;
                slot.active       = false;
                --live_count;
                ++orders_cancelled;
                continue;
            }

            // Attempt limit fill
            FillResult fr = fill_model.process_limit_order(
                                slot.order, slot.queue, tick);

            if (fr.filled_qty > 0) {
                update_position(slot.order, fr.filled_qty, fr.fill_price);
            }

            if (slot.order.is_filled()) {
                ++orders_filled;
                slot.active = false;
                --live_count;
            }
        }
    }

    // ── PnL helpers ─────────────────────────────────────────
    inline double compute_unrealised_pnl(double mark_price) const noexcept {
        if (position == 0) return 0.0;
        // unrealised = position * (mark - avg_entry)
        double avg_entry = (position != 0)
            ? entry_notional / std::abs(position) : 0.0;
        return position * (mark_price - avg_entry);
    }

    inline double total_pnl(double mark_price) const noexcept {
        return realised_pnl + compute_unrealised_pnl(mark_price);
    }

    // ── Status ──────────────────────────────────────────────
    inline int  live_orders()    const noexcept { return live_count; }
    inline bool has_capacity()   const noexcept { return live_count < EngineConfig::MAX_LIVE_ORDERS; }

private:

    // ── Risk gate ────────────────────────────────────────────
    inline bool passes_risk_checks(const Signal& sig,
                                   ExecutionReport& rpt) const noexcept
    {
        if (sig.is_flat()) {
            rpt.rejected = true;
            std::strncpy(rpt.reject_reason, "FLAT_SIGNAL", 31);
            return false;
        }
        if (sig.risk > cfg.max_risk_threshold) {
            rpt.rejected = true;
            std::strncpy(rpt.reject_reason, "RISK_TOO_HIGH", 31);
            return false;
        }
        if (sig.strength() < cfg.min_alpha_threshold) {
            rpt.rejected = true;
            std::strncpy(rpt.reject_reason, "ALPHA_TOO_SMALL", 31);
            return false;
        }
        if (sig.confidence < cfg.min_confidence) {
            rpt.rejected = true;
            std::strncpy(rpt.reject_reason, "LOW_CONFIDENCE", 31);
            return false;
        }
        if (!has_capacity()) {
            rpt.rejected = true;
            std::strncpy(rpt.reject_reason, "ORDER_POOL_FULL", 31);
            return false;
        }
        return true;
    }

    // ── Order sizing: confidence-scaled ──────────────────────
    inline int compute_order_size(const Signal& sig) const noexcept {
        // size = base * confidence * |alpha|, capped at max
        double scale = sig.confidence * sig.strength();
        int    size  = static_cast<int>(cfg.base_order_size * scale * 10.0);
        size = std::max(size, 1);
        size = std::min(size, cfg.max_order_size);
        return size;
    }

    // ── Order type selection ─────────────────────────────────
    inline OType choose_order_type(const Signal& sig) const noexcept {
        if (sig.urgency >= cfg.market_order_urgency)          return OType::MARKET;
        if (sig.strength() >= cfg.market_order_alpha)         return OType::MARKET;
        return OType::LIMIT;
    }

    // ── Limit price: join best or cross spread ────────────────
    inline double choose_limit_price(Side side, OType type,
                                     double /*impact*/,
                                     int64_t /*ts_ns*/) const noexcept
    {
        if (type == OType::MARKET) return 0.0;  // not used for market orders
        // Passive limit: join the best bid/ask
        if (side == Side::BUY)  return book->best_bid();
        else                    return book->best_ask();
    }

    // ── Position limit check ─────────────────────────────────
    inline bool position_allows(Side side, int size) const noexcept {
        int new_pos = position + (side == Side::BUY ? +size : -size);
        return std::abs(new_pos) <= cfg.max_position;
    }

    // ── Update position & PnL on fill ────────────────────────
    inline void update_position(const Order& order,
                                int fill_qty, double fill_px) noexcept
    {
        int signed_qty = (order.side == Side::BUY) ? +fill_qty : -fill_qty;
        double notional = fill_px * fill_qty;

        // If fill reduces existing position: realise PnL
        bool reducing = (position > 0 && order.side == Side::SELL) ||
                        (position < 0 && order.side == Side::BUY);

        if (reducing) {
            double avg_entry = (position != 0)
                ? entry_notional / std::abs(position) : fill_px;
            double pnl_per_unit = (order.side == Side::SELL)
                ? (fill_px - avg_entry)
                : (avg_entry - fill_px);
            realised_pnl  += pnl_per_unit * fill_qty;
            entry_notional -= avg_entry * fill_qty;
        } else {
            // Adding to position
            entry_notional += notional;
        }

        position += signed_qty;

        // Keep entry_notional consistent with flat position
        if (position == 0) entry_notional = 0.0;
    }

    // ── Cancellation logic ───────────────────────────────────
    inline bool should_cancel(const Order& order,
                               const MarketTick& tick) const noexcept
    {
        // Timeout check
        int64_t age_ns = tick.timestamp_ns - order.timestamp_ns;
        if (age_ns > cfg.limit_order_timeout_ns) return true;

        // Adverse price movement check
        if (order.side == Side::BUY) {
            // If best ask has moved far above our limit, cancel
            double move = tick.best_ask - order.price;
            if (move > cfg.adverse_move_ticks * cfg.tick_size) return true;
        } else {
            // If best bid has moved far below our limit, cancel
            double move = order.price - tick.best_bid;
            if (move > cfg.adverse_move_ticks * cfg.tick_size) return true;
        }

        return false;
    }

    // ── Pool management (O(1), no heap) ──────────────────────
    inline int allocate_slot() noexcept {
        for (int i = 0; i < EngineConfig::MAX_LIVE_ORDERS; ++i) {
            if (!order_pool[i].active) return i;
        }
        return -1;   // pool exhausted
    }
};

} // namespace hft