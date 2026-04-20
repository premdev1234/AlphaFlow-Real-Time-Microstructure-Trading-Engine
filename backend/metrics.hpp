#pragma once
// =============================================================================
// metrics.hpp — Online Performance Metrics for Backtesting
// =============================================================================
// All metrics are computed incrementally (O(1) per update) to avoid
// re-scanning trade history in the hot path.
//
// Design:
//  - EquityMetrics  : tracks equity curve, drawdown, return stream
//  - TradeMetrics   : tracks per-trade stats (win rate, PF, expectancy)
//  - RollingMetrics : windowed Sharpe / vol (fixed-size ring, no heap alloc)
//  - MetricsSnapshot: POD summary for logging / walk-forward comparison
// =============================================================================

#include <cmath>
#include <cstdint>
#include <array>
#include <algorithm>
#include <limits>
#include <string_view>

namespace qts {

// ---------------------------------------------------------------------------
// Welford's online algorithm for mean + variance (no heap, no history)
// ---------------------------------------------------------------------------
struct OnlineStats {
    double mean   = 0.0;
    double M2     = 0.0;   // sum of squared deviations
    int64_t count = 0;

    void update(double x) noexcept {
        ++count;
        double delta  = x - mean;
        mean         += delta / static_cast<double>(count);
        double delta2 = x - mean;
        M2           += delta * delta2;
    }

    [[nodiscard]] double variance() const noexcept {
        return (count > 1) ? M2 / static_cast<double>(count - 1) : 0.0;
    }

    [[nodiscard]] double stddev() const noexcept {
        return std::sqrt(variance());
    }

    void reset() noexcept { *this = {}; }
};

// ---------------------------------------------------------------------------
// Fixed-size circular buffer for rolling window stats (stack-allocated)
// ---------------------------------------------------------------------------
template <std::size_t N>
struct RollingWindow {
    static_assert(N > 0 && (N & (N - 1)) == 0, "N must be power of 2");

    std::array<double, N> buf{};
    std::size_t head = 0;
    std::size_t count = 0;
    double sum   = 0.0;
    double sumSq = 0.0;

    void push(double x) noexcept {
        if (count == N) {
            // evict oldest
            double old  = buf[head];
            sum        -= old;
            sumSq      -= old * old;
        } else {
            ++count;
        }
        buf[head] = x;
        sum      += x;
        sumSq    += x * x;
        head      = (head + 1) & (N - 1);
    }

    [[nodiscard]] bool full()  const noexcept { return count == N; }
    [[nodiscard]] double mean()  const noexcept {
        return (count > 0) ? sum / static_cast<double>(count) : 0.0;
    }
    [[nodiscard]] double variance() const noexcept {
        if (count < 2) return 0.0;
        double n = static_cast<double>(count);
        return (sumSq - sum * sum / n) / (n - 1.0);
    }
    [[nodiscard]] double stddev() const noexcept { return std::sqrt(variance()); }

    void reset() noexcept { buf.fill(0.0); head = 0; count = 0; sum = 0.0; sumSq = 0.0; }
};

// ---------------------------------------------------------------------------
// Equity-curve tracker (peak, drawdown, returns)
// ---------------------------------------------------------------------------
struct EquityMetrics {
    double equity      = 0.0;   // current equity (cumulative PnL)
    double peak        = 0.0;   // high-water mark
    double maxDrawdown = 0.0;   // worst trough-to-peak in $ terms
    double maxDDPct    = 0.0;   // worst drawdown as fraction of peak

    int64_t barCount   = 0;
    OnlineStats returnStats;    // per-bar return statistics

    // Call once per "bar" or tick with the PnL delta for that period.
    void update(double pnlDelta) noexcept {
        equity += pnlDelta;
        ++barCount;

        if (equity > peak) peak = equity;

        double dd = peak - equity;
        if (dd > maxDrawdown) maxDrawdown = dd;

        if (peak != 0.0) {
            double ddPct = dd / std::abs(peak);
            if (ddPct > maxDDPct) maxDDPct = ddPct;
        }

        returnStats.update(pnlDelta);
    }

    // Annualised Sharpe — caller supplies periods_per_year (e.g. 252 for daily)
    [[nodiscard]] double sharpe(double periodsPerYear = 252.0) const noexcept {
        double sd = returnStats.stddev();
        if (sd < 1e-12) return 0.0;
        return (returnStats.mean / sd) * std::sqrt(periodsPerYear);
    }

    void reset() noexcept { *this = {}; }
};

// ---------------------------------------------------------------------------
// Per-trade statistics tracker
// ---------------------------------------------------------------------------
struct TradeMetrics {
    int64_t totalTrades  = 0;
    int64_t winningTrades = 0;
    int64_t losingTrades  = 0;

    double grossProfit   = 0.0;   // sum of positive PnLs
    double grossLoss     = 0.0;   // sum of |negative PnLs|

    OnlineStats pnlStats;         // mean + var of per-trade PnL

    double maxWin  = -std::numeric_limits<double>::max();
    double maxLoss =  std::numeric_limits<double>::max();

    // Call once per completed trade.
    void recordTrade(double tradePnl) noexcept {
        ++totalTrades;
        pnlStats.update(tradePnl);

        if (tradePnl > 0.0) {
            ++winningTrades;
            grossProfit += tradePnl;
            if (tradePnl > maxWin) maxWin = tradePnl;
        } else if (tradePnl < 0.0) {
            ++losingTrades;
            grossLoss += (-tradePnl);
            if (tradePnl < maxLoss) maxLoss = tradePnl;
        }
    }

    [[nodiscard]] double winRate() const noexcept {
        if (totalTrades == 0) return 0.0;
        return static_cast<double>(winningTrades) / static_cast<double>(totalTrades);
    }

    // gross_profit / gross_loss  (infinity if no losses)
    [[nodiscard]] double profitFactor() const noexcept {
        if (grossLoss < 1e-12) return (grossProfit > 0.0)
            ? std::numeric_limits<double>::infinity() : 0.0;
        return grossProfit / grossLoss;
    }

    // Average profit per trade (including losers)
    [[nodiscard]] double expectancy() const noexcept {
        return pnlStats.mean;
    }

    void reset() noexcept { *this = {}; }
};

// ---------------------------------------------------------------------------
// Rolling Sharpe (last N bars) — fully stack-allocated
// ---------------------------------------------------------------------------
template <std::size_t N = 64>
struct RollingSharpe {
    RollingWindow<N> window;

    void update(double pnlDelta) noexcept { window.push(pnlDelta); }

    [[nodiscard]] double sharpe(double periodsPerYear = 252.0) const noexcept {
        double sd = window.stddev();
        if (sd < 1e-12) return 0.0;
        return (window.mean() / sd) * std::sqrt(periodsPerYear);
    }

    void reset() noexcept { window.reset(); }
};

// ---------------------------------------------------------------------------
// Aggregate snapshot (POD — safe to memcpy / write to disk)
// ---------------------------------------------------------------------------
struct MetricsSnapshot {
    // equity / drawdown
    double totalPnl        = 0.0;
    double peakEquity      = 0.0;
    double maxDrawdownAbs  = 0.0;
    double maxDrawdownPct  = 0.0;

    // return stats
    double meanReturn      = 0.0;
    double stdReturn       = 0.0;
    double sharpeRatio     = 0.0;

    // trade stats
    int64_t totalTrades    = 0;
    double  winRate        = 0.0;
    double  profitFactor   = 0.0;
    double  expectancy     = 0.0;
    double  maxWin         = 0.0;
    double  maxLoss        = 0.0;

    // timing
    int64_t barCount       = 0;
};

// ---------------------------------------------------------------------------
// Aggregator: combines EquityMetrics + TradeMetrics → MetricsSnapshot
// ---------------------------------------------------------------------------
struct MetricsEngine {
    EquityMetrics  equity;
    TradeMetrics   trades;

    void onBar(double pnlDelta) noexcept      { equity.update(pnlDelta); }
    void onTrade(double tradePnl) noexcept    { trades.recordTrade(tradePnl); }

    [[nodiscard]] MetricsSnapshot snapshot(double periodsPerYear = 252.0) const noexcept {
        MetricsSnapshot s{};
        s.totalPnl       = equity.equity;
        s.peakEquity     = equity.peak;
        s.maxDrawdownAbs = equity.maxDrawdown;
        s.maxDrawdownPct = equity.maxDDPct;
        s.meanReturn     = equity.returnStats.mean;
        s.stdReturn      = equity.returnStats.stddev();
        s.sharpeRatio    = equity.sharpe(periodsPerYear);
        s.totalTrades    = trades.totalTrades;
        s.winRate        = trades.winRate();
        s.profitFactor   = trades.profitFactor();
        s.expectancy     = trades.expectancy();
        s.maxWin         = (trades.totalTrades > 0) ? trades.maxWin  : 0.0;
        s.maxLoss        = (trades.totalTrades > 0) ? trades.maxLoss : 0.0;
        s.barCount       = equity.barCount;
        return s;
    }

    void reset() noexcept { equity.reset(); trades.reset(); }
};

} // namespace qts
