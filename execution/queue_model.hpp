// queue_model.hpp
#pragma once
// ============================================================
//  queue_model.hpp  –  HFT Execution Module
//  FIFO queue position model for limit orders.
//
//  RATIONALE:
//  A limit order placed on a price level must wait for all
//  orders ahead of it to be matched before it receives a fill.
//  We track:
//    ahead_qty   – volume ahead in the queue at arrival
//    behind_qty  – volume behind (informational only)
//    filled_ahead – cumulative trade volume that cleared queue
//
//  When filled_ahead >= ahead_qty the order is at the front
//  and may receive fills proportional to remaining depth.
// ============================================================

#include <cstdint>
#include <algorithm>

namespace hft {

// ─── QueueModel ─────────────────────────────────────────────
//  One instance per live limit order.
//  Zero heap allocations; entirely stack/struct resident.
struct QueueModel {

    // ── Configuration (set at order placement) ───────────────
    int     ahead_qty;         // liquidity ahead of us at entry
    int     behind_qty;        // liquidity behind us at entry
    int     order_qty;         // our order size

    // ── Runtime state ────────────────────────────────────────
    int     filled_ahead;      // cumulative volume traded at our level
    int     queue_filled_qty;  // how much of OUR order has been filled
    bool    at_front;          // true once queue clears ahead_qty

    // ── Construction ─────────────────────────────────────────
    static QueueModel make(int order_size,
                           int liquidity_ahead,
                           int liquidity_behind) noexcept
    {
        QueueModel qm{};
        qm.ahead_qty       = liquidity_ahead;
        qm.behind_qty      = liquidity_behind;
        qm.order_qty       = order_size;
        qm.filled_ahead    = 0;
        qm.queue_filled_qty = 0;
        qm.at_front        = (liquidity_ahead == 0);
        return qm;
    }

    // ── update(trade_volume) ─────────────────────────────────
    //  Called each time a trade occurs at our price level.
    //  `trade_vol`  – number of contracts that traded this tick.
    //  Returns the number of units filled from OUR order this tick.
    //
    //  Algorithm:
    //  1. Absorb volume into `filled_ahead` until queue clears.
    //  2. Any excess volume beyond queue fills our order (pro-rata
    //     with other orders behind us is ignored in this model –
    //     we receive fills first once at front, conservative).
    inline int update(int trade_vol) noexcept {
        if (trade_vol <= 0 || queue_filled_qty >= order_qty)
            return 0;

        int our_fill = 0;

        if (!at_front) {
            // Absorb into ahead queue
            int remaining_ahead = ahead_qty - filled_ahead;
            int absorbed = std::min(trade_vol, remaining_ahead);
            filled_ahead += absorbed;
            trade_vol    -= absorbed;

            if (filled_ahead >= ahead_qty) {
                at_front = true;
            }
        }

        // If we've cleared the queue, trade volume now hits our order
        if (at_front && trade_vol > 0) {
            int can_fill = order_qty - queue_filled_qty;
            our_fill         = std::min(trade_vol, can_fill);
            queue_filled_qty += our_fill;
        }

        return our_fill;
    }

    // ── update_with_cancel(cancel_vol) ───────────────────────
    //  Cancellations ahead of us reduce our waiting time.
    //  Treat as equivalent to trades for queue position.
    inline void apply_cancellation_ahead(int cancel_vol) noexcept {
        if (at_front) return;
        int remaining_ahead = ahead_qty - filled_ahead;
        int removed = std::min(cancel_vol, remaining_ahead);
        filled_ahead += removed;          // treat cancels as consumed
        if (filled_ahead >= ahead_qty)
            at_front = true;
    }

    // ── Accessors ────────────────────────────────────────────
    inline bool is_at_front() const noexcept { return at_front; }

    inline bool is_filled() const noexcept {
        return queue_filled_qty >= order_qty;
    }

    // Fraction of the queue cleared in front of us [0, 1]
    inline double queue_progress() const noexcept {
        if (ahead_qty == 0) return 1.0;
        double prog = static_cast<double>(filled_ahead) / ahead_qty;
        return prog > 1.0 ? 1.0 : prog;
    }

    // Estimated time-to-fill in "trade events" (linear extrapolation)
    // Returns -1 if already at front
    inline double estimated_ticks_to_front(double avg_trade_vol_per_tick) const noexcept {
        if (at_front) return 0.0;
        if (avg_trade_vol_per_tick <= 0.0) return -1.0;
        int remaining_ahead = ahead_qty - filled_ahead;
        return static_cast<double>(remaining_ahead) / avg_trade_vol_per_tick;
    }
};

} // namespace hft