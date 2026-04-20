#pragma once
// =============================================================================
// trade_log.hpp — Fixed-capacity Trade Journal
// =============================================================================
// Stores every execution event without heap allocation up to Capacity.
// Designed for post-run analysis, replay, and regime attribution.
//
// TradeRecord : one completed round-trip (or one-sided fill for open positions)
// TradeLog    : circular or linear (throw-away oldest) store
// =============================================================================

#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <cassert>

namespace qts {

// ---------------------------------------------------------------------------
// Side enum
// ---------------------------------------------------------------------------
enum class Side : int8_t { Buy = 1, Sell = -1, None = 0 };

[[nodiscard]] constexpr std::string_view sideStr(Side s) noexcept {
    switch (s) {
        case Side::Buy:  return "BUY";
        case Side::Sell: return "SELL";
        default:         return "NONE";
    }
}

// ---------------------------------------------------------------------------
// A single fill record — POD, 64 bytes, cache-line friendly
// ---------------------------------------------------------------------------
struct TradeRecord {
    int64_t  timestamp   = 0;       // nanoseconds since epoch
    double   fillPrice   = 0.0;
    double   quantity    = 0.0;     // always positive
    double   pnl         = 0.0;     // realised PnL of this fill
    double   cumulPnl    = 0.0;     // equity at this point
    double   signal      = 0.0;     // raw signal that triggered the order
    Side     side        = Side::None;
    uint8_t  regimeId    = 0;       // filled in by regime detector
    uint16_t strategyId  = 0;       // which strategy produced the signal
    uint8_t  _pad[5]     = {};      // keep struct 64-byte aligned
};
static_assert(sizeof(TradeRecord) == 64, "TradeRecord must be 64 bytes");

// ---------------------------------------------------------------------------
// Fixed-capacity trade log (no dynamic allocation)
// ---------------------------------------------------------------------------
template <std::size_t Capacity = 65536>
class TradeLog {
public:
    static constexpr std::size_t capacity = Capacity;

    // Returns false if the log is full and overwrote the oldest entry.
    bool record(const TradeRecord& rec) noexcept {
        store_[writeIdx_ & mask_] = rec;
        ++writeIdx_;
        if (size_ < Capacity) ++size_;
        return size_ <= Capacity;
    }

    // Convenience builder — caller fills what it knows.
    bool record(int64_t ts, Side side, double price, double qty,
                double pnl, double cumulPnl,
                double signal = 0.0,
                uint8_t  regimeId   = 0,
                uint16_t strategyId = 0) noexcept {
        TradeRecord r{};
        r.timestamp  = ts;
        r.side       = side;
        r.fillPrice  = price;
        r.quantity   = qty;
        r.pnl        = pnl;
        r.cumulPnl   = cumulPnl;
        r.signal     = signal;
        r.regimeId   = regimeId;
        r.strategyId = strategyId;
        return record(r);
    }

    // ---- accessors --------------------------------------------------------

    [[nodiscard]] std::size_t size()  const noexcept { return size_; }
    [[nodiscard]] bool        empty() const noexcept { return size_ == 0; }
    [[nodiscard]] bool        full()  const noexcept { return size_ == Capacity; }

    // Access i-th element in insertion order (0 = oldest still retained).
    [[nodiscard]] const TradeRecord& operator[](std::size_t i) const noexcept {
        assert(i < size_);
        std::size_t startIdx = (writeIdx_ >= Capacity) ? writeIdx_ - Capacity : 0;
        return store_[(startIdx + i) & mask_];
    }

    // Most recent record.
    [[nodiscard]] const TradeRecord& back() const noexcept {
        assert(size_ > 0);
        return store_[(writeIdx_ - 1) & mask_];
    }

    // ---- iteration --------------------------------------------------------

    // Calls f(const TradeRecord&) for each record in insertion order.
    template <typename Fn>
    void forEach(Fn&& f) const noexcept(noexcept(f(std::declval<TradeRecord>()))) {
        std::size_t n = size_;
        std::size_t startIdx = (writeIdx_ >= Capacity) ? writeIdx_ - Capacity : 0;
        for (std::size_t i = 0; i < n; ++i)
            f(store_[(startIdx + i) & mask_]);
    }

    // Filter by regime.
    template <typename Fn>
    void forEachInRegime(uint8_t regimeId, Fn&& f) const
        noexcept(noexcept(f(std::declval<TradeRecord>()))) {
        forEach([&](const TradeRecord& r) {
            if (r.regimeId == regimeId) f(r);
        });
    }

    void clear() noexcept {
        writeIdx_ = 0;
        size_     = 0;
    }

private:
    static constexpr std::size_t mask_ = Capacity - 1;
    static_assert((Capacity & mask_) == 0, "Capacity must be a power of 2");

    std::array<TradeRecord, Capacity> store_{};
    std::size_t writeIdx_ = 0;
    std::size_t size_     = 0;
};

} // namespace qts
