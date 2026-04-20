#pragma once
// =============================================================================
// regime.hpp — Market Regime Detector
// =============================================================================
// Classifies the current market into volatility regimes using a rolling
// window of mid-price returns. Classification is threshold-based and can
// be extended to more regimes (e.g. trending / mean-reverting) trivially.
//
// Components:
//  - RegimeId enum      : numeric labels (stable, fits in uint8_t)
//  - RegimeDetector<N>  : computes rolling vol, emits regime label
//  - RegimeStats        : per-regime MetricsEngine (for attribution)
//  - RegimeTracker      : wires detector → per-regime stats
// =============================================================================

#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>
#include "metrics.hpp"

namespace qts {

// ---------------------------------------------------------------------------
// Regime labels — deliberately small so they fit in TradeRecord::regimeId
// ---------------------------------------------------------------------------
enum class RegimeId : uint8_t {
    Unknown      = 0,
    LowVol       = 1,
    NormalVol    = 2,
    HighVol      = 3,
    // Extend as needed, e.g.: Trending = 4, MeanReverting = 5
};

[[nodiscard]] constexpr std::string_view regimeName(RegimeId r) noexcept {
    switch (r) {
        case RegimeId::LowVol:    return "LOW_VOL";
        case RegimeId::NormalVol: return "NORMAL_VOL";
        case RegimeId::HighVol:   return "HIGH_VOL";
        default:                  return "UNKNOWN";
    }
}

// ---------------------------------------------------------------------------
// Regime thresholds (tunable at construction time)
// ---------------------------------------------------------------------------
struct RegimeThresholds {
    double lowVolMax    = 0.001;   // annualised vol fraction below which → LowVol
    double highVolMin   = 0.003;   // above which → HighVol
    // between the two → NormalVol
};

// ---------------------------------------------------------------------------
// Rolling volatility + regime classification
//   N      : window size for return calculation (must be power-of-2)
//   Returns are computed as log(mid_t / mid_{t-1})
// ---------------------------------------------------------------------------
template <std::size_t N = 64>
class RegimeDetector {
public:
    explicit RegimeDetector(RegimeThresholds thr = {}) noexcept : thr_(thr) {}

    // Feed a new mid price.  Returns current regime.
    RegimeId update(double midPrice) noexcept {
        if (prevMid_ > 0.0) {
            double ret = midPrice - prevMid_;   // use abs return for vol
            window_.push(ret);
        }
        prevMid_ = midPrice;

        // Only classify once window has enough data
        if (window_.count < 4) {
            current_ = RegimeId::Unknown;
            return current_;
        }

        double vol = window_.stddev();
        classify(vol);
        return current_;
    }

    [[nodiscard]] RegimeId  current()    const noexcept { return current_; }
    [[nodiscard]] double    rollingVol() const noexcept { return window_.stddev(); }

    void reset() noexcept {
        window_.reset();
        prevMid_ = 0.0;
        current_ = RegimeId::Unknown;
    }

private:
    void classify(double vol) noexcept {
        if (vol < thr_.lowVolMax)
            current_ = RegimeId::LowVol;
        else if (vol > thr_.highVolMin)
            current_ = RegimeId::HighVol;
        else
            current_ = RegimeId::NormalVol;
    }

    RollingWindow<N> window_;
    double           prevMid_  = 0.0;
    RegimeId         current_  = RegimeId::Unknown;
    RegimeThresholds thr_;
};

// ---------------------------------------------------------------------------
// Per-regime performance attribution
// ---------------------------------------------------------------------------
struct RegimeMetrics {
    RegimeId      id{};
    MetricsEngine metrics{};
    int64_t       barsInRegime  = 0;
    int64_t       tradesInRegime = 0;
};

// ---------------------------------------------------------------------------
// RegimeTracker: maintains one MetricsEngine per regime label
//   MaxRegimes must be >= number of RegimeId values used
// ---------------------------------------------------------------------------
template <std::size_t MaxRegimes = 8>
class RegimeTracker {
public:
    RegimeTracker() noexcept {
        // initialise IDs
        for (std::size_t i = 0; i < MaxRegimes; ++i)
            slots_[i].id = static_cast<RegimeId>(i);
    }

    // Called once per bar (tick / period)
    void onBar(RegimeId regime, double pnlDelta) noexcept {
        auto& s = slot(regime);
        s.metrics.onBar(pnlDelta);
        ++s.barsInRegime;
    }

    // Called once per completed trade
    void onTrade(RegimeId regime, double tradePnl) noexcept {
        auto& s = slot(regime);
        s.metrics.onTrade(tradePnl);
        ++s.tradesInRegime;
    }

    [[nodiscard]] const RegimeMetrics& statsFor(RegimeId r) const noexcept {
        return slots_[static_cast<std::size_t>(r)];
    }

    // Iterate all regime slots
    template <typename Fn>
    void forEach(Fn&& f) const noexcept(noexcept(f(std::declval<RegimeMetrics>()))) {
        for (std::size_t i = 0; i < MaxRegimes; ++i)
            f(slots_[i]);
    }

    void reset() noexcept {
        for (auto& s : slots_) {
            s.metrics.reset();
            s.barsInRegime   = 0;
            s.tradesInRegime = 0;
        }
    }

private:
    RegimeMetrics& slot(RegimeId r) noexcept {
        return slots_[static_cast<std::size_t>(r)];
    }

    std::array<RegimeMetrics, MaxRegimes> slots_{};
};

} // namespace qts
