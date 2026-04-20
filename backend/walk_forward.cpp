#pragma once


#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include "metrics.hpp"
#include "backtester.hpp"
#include <memory>

namespace qts {

// ---------------------------------------------------------------------------
// Per-window result
// ---------------------------------------------------------------------------
struct WalkForwardWindow {
    std::size_t     windowIndex    = 0;
    std::size_t     trainBegin     = 0;   // tick index
    std::size_t     trainEnd       = 0;
    std::size_t     testBegin      = 0;
    std::size_t     testEnd        = 0;
    MetricsSnapshot testMetrics{};
    MetricsSnapshot trainMetrics{};       // populated only if cfg.evalTrain = true
    bool            valid          = false;
};

// ---------------------------------------------------------------------------
// Walk-forward configuration
// ---------------------------------------------------------------------------
struct WalkForwardConfig {
    std::size_t trainSize = 10'000;   // ticks in the training window
    std::size_t testSize  =  2'000;   // ticks in the test (OOS) window
    std::size_t stepSize  =  2'000;   // how far to advance each iteration
                                       // (stepSize == testSize → anchored expanding)
                                       // (stepSize <  testSize → overlapping OOS)
    bool evalTrain        = false;     // also backtest training window (for IS stats)
    double periodsPerYear = 252.0;
};

// ---------------------------------------------------------------------------
// Walk-forward engine
// ---------------------------------------------------------------------------
template <
    typename Tick,
    typename FeatureEngine,
    typename StrategyAggregator,
    typename ExecutionEngine,
    std::size_t MaxWindows = 256
>
class WalkForward {
public:
    using BacktestT = Backtester<Tick, FeatureEngine, StrategyAggregator, ExecutionEngine>;
    using CalibrateFn = std::function<void(const Tick* begin, const Tick* end)>;

    WalkForward(FeatureEngine&      features,
                StrategyAggregator& aggregator,
                ExecutionEngine&    execution,
                WalkForwardConfig   wfCfg  = {},
                BacktestConfig      btCfg  = {}) noexcept
        : backtester_(features, aggregator, execution, btCfg)
        , wfCfg_(wfCfg)
    {
        btCfg_ = btCfg;
        btCfg_.periodsPerYear = wfCfg.periodsPerYear;
    }

    // Set calibration hook.  Called before each test window.
    void setCalibrateFn(CalibrateFn fn) noexcept { calibrateFn_ = std::move(fn); }

    // -----------------------------------------------------------------------
    // Run the full walk-forward loop over a contiguous tick array.
    // Returns number of windows executed.
    // -----------------------------------------------------------------------
    std::size_t run(const Tick* ticks, std::size_t totalTicks) noexcept {
        numWindows_ = 0;

        std::size_t pos = 0;

        while (pos + wfCfg_.trainSize + wfCfg_.testSize <= totalTicks
               && numWindows_ < MaxWindows) {

            WalkForwardWindow& w = windows_[numWindows_];
            w.windowIndex = numWindows_;
            w.trainBegin  = pos;
            w.trainEnd    = pos + wfCfg_.trainSize;
            w.testBegin   = w.trainEnd;
            w.testEnd     = w.testBegin + wfCfg_.testSize;

            // --- Calibration phase ---
            if (calibrateFn_) {
                calibrateFn_(ticks + w.trainBegin, ticks + w.trainEnd);
            }

            // --- Optional: IS backtest ---
            if (wfCfg_.evalTrain) {
                backtester_.reset();
                backtester_.runRange(ticks + w.trainBegin, ticks + w.trainEnd);
                w.trainMetrics = backtester_.finalise().summary;
            }

            // --- OOS backtest ---
            backtester_.reset();
            backtester_.runRange(ticks + w.testBegin, ticks + w.testEnd);
            w.testMetrics = backtester_.finalise().summary;
            w.valid = true;

            ++numWindows_;
            pos += wfCfg_.stepSize;
        }

        return numWindows_;
    }

    // -----------------------------------------------------------------------
    // Aggregate OOS statistics across all windows (simple mean)
    // -----------------------------------------------------------------------
    [[nodiscard]] MetricsSnapshot aggregateOOS() const noexcept {
        if (numWindows_ == 0) return {};

        OnlineStats sharpe, pnl, winRate, pf, dd;
        for (std::size_t i = 0; i < numWindows_; ++i) {
            const auto& m = windows_[i].testMetrics;
            sharpe.update(m.sharpeRatio);
            pnl.update(m.totalPnl);
            winRate.update(m.winRate);
            pf.update(m.profitFactor);
            dd.update(m.maxDrawdownPct);
        }

        MetricsSnapshot agg{};
        agg.sharpeRatio    = sharpe.mean;
        agg.totalPnl       = pnl.mean;
        agg.winRate        = winRate.mean;
        agg.profitFactor   = pf.mean;
        agg.maxDrawdownPct = dd.mean;
        agg.barCount       = static_cast<int64_t>(numWindows_);  // reuse as window count
        return agg;
    }

    // -----------------------------------------------------------------------
    // Ratio: mean OOS Sharpe / std OOS Sharpe  (robustness score > 1 is good)
    // -----------------------------------------------------------------------
    [[nodiscard]] double sharpeConsistency() const noexcept {
        OnlineStats s;
        for (std::size_t i = 0; i < numWindows_; ++i)
            s.update(windows_[i].testMetrics.sharpeRatio);
        double sd = s.stddev();
        return (sd < 1e-12) ? 0.0 : s.mean / sd;
    }

    // ---- Accessors ---------------------------------------------------------

    [[nodiscard]] std::size_t windowCount() const noexcept { return numWindows_; }

    [[nodiscard]] const WalkForwardWindow& window(std::size_t i) const noexcept {
        return windows_[i];
    }

    template <typename Fn>
    void forEach(Fn&& f) const noexcept(noexcept(f(std::declval<WalkForwardWindow>()))) {
        for (std::size_t i = 0; i < numWindows_; ++i)
            f(windows_[i]);
    }

private:
    BacktestT             backtester_;
    WalkForwardConfig     wfCfg_;
    BacktestConfig        btCfg_;
    CalibrateFn           calibrateFn_;

    std::array<WalkForwardWindow, MaxWindows> windows_{};
    std::size_t numWindows_ = 0;
};

} // namespace qts
