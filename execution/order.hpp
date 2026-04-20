// order.hpp
#pragma once
// ============================================================
//  order.hpp  –  HFT Execution Module
//  Order definition, lifecycle helpers, and compact metadata.
//  DESIGN: POD-friendly struct, no virtual, no heap alloc.
// ============================================================

#include <cstdint>
#include <cstring>
#include <cassert>

namespace hft {

// ─── Enums ──────────────────────────────────────────────────
enum class Side  : uint8_t { BUY = 0, SELL = 1 };
enum class OType : uint8_t { LIMIT = 0, MARKET = 1 };

enum class OrderStatus : uint8_t {
    PENDING   = 0,
    LIVE      = 1,
    PARTIAL   = 2,
    FILLED    = 3,
    CANCELLED = 4,
    REJECTED  = 5
};

// ─── Fill record (ring-buffer friendly, 32 bytes) ───────────
struct FillRecord {
    uint64_t order_id;
    int64_t  timestamp_ns;
    double   fill_price;
    int      fill_qty;
    uint32_t _pad;                // align to 32 bytes
};
static_assert(sizeof(FillRecord) == 32, "FillRecord must be 32 bytes");

// ─── Core Order (cache-line friendly: 64 bytes) ─────────────
// Layout (LP64):
//   id(8) + strategy_id(8) + price(8) + avg_fill(8)      = 32
//   timestamp_ns(8) + last_update_ns(8)                   = 16
//   quantity(4) + remaining_qty(4) + filled_qty(4)        = 12
//   side(1) + type(1) + status(1) + _pad(1)               =  4
//   Total                                                  = 64 ✓
struct Order {
    // --- identity ---
    uint64_t id;             // 0
    uint64_t strategy_id;    // 8

    // --- market parameters ---
    double   price;          // 16 – limit price (ignored for MARKET)
    double   avg_fill_price; // 24 – volume-weighted fill price so far

    // --- timing (8-byte aligned together) ---
    int64_t  timestamp_ns;   // 32 – submission time (ns epoch)
    int64_t  last_update_ns; // 40 – last fill/status change

    // --- quantities (4-byte fields together) ---
    int      quantity;       // 48 – original quantity
    int      remaining_qty;  // 52 – units still unfilled
    int      filled_qty;     // 56 – units filled so far

    // --- classification (1-byte fields, packed) ---
    Side        side;        // 60
    OType       type;        // 61
    OrderStatus status;      // 62
    uint8_t     _pad;        // 63

    // ── Construction helper (avoids designated-init for C++17) ──
    static Order make(uint64_t oid, uint64_t strat_id,
                      Side s, OType t,
                      double px, int qty,
                      int64_t ts_ns) noexcept
    {
        Order o{};
        o.id              = oid;
        o.strategy_id     = strat_id;
        o.price           = px;
        o.avg_fill_price  = 0.0;
        o.quantity        = qty;
        o.remaining_qty   = qty;
        o.filled_qty      = 0;
        o.timestamp_ns    = ts_ns;
        o.last_update_ns  = ts_ns;
        o.side            = s;
        o.type            = t;
        o.status          = OrderStatus::PENDING;
        return o;
    }

    // ── Lifecycle helpers (inline, no alloc) ────────────────────

    // Apply a fill of `qty` units at `fill_px`.
    // Updates VWAP, remaining_qty, status.
    inline void fill(int qty, double fill_px, int64_t ts_ns) noexcept {
        assert(qty > 0 && qty <= remaining_qty);
        // Update volume-weighted average fill price
        double prev_notional = avg_fill_price * filled_qty;
        filled_qty     += qty;
        remaining_qty  -= qty;
        avg_fill_price  = (prev_notional + fill_px * qty) / filled_qty;
        last_update_ns  = ts_ns;
        status = (remaining_qty == 0) ? OrderStatus::FILLED
                                      : OrderStatus::PARTIAL;
    }

    inline bool is_filled()   const noexcept { return remaining_qty == 0; }
    inline bool is_live()     const noexcept {
        return status == OrderStatus::LIVE || status == OrderStatus::PARTIAL;
    }
    inline bool is_terminal() const noexcept {
        return status == OrderStatus::FILLED  ||
               status == OrderStatus::CANCELLED ||
               status == OrderStatus::REJECTED;
    }

    // Fill ratio in [0.0, 1.0]
    inline double fill_ratio() const noexcept {
        return (quantity > 0)
            ? static_cast<double>(filled_qty) / quantity
            : 0.0;
    }

    // Notional value of executed portion
    inline double filled_notional() const noexcept {
        return avg_fill_price * filled_qty;
    }
};
static_assert(sizeof(Order) == 64, "Order must fit in one cache line (64 bytes)");

} // namespace hft