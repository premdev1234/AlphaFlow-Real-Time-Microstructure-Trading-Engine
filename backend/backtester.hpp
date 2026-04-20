#pragma once
#include <array>
#include <cstdint>
#include <cmath>
#include <iostream>

#include "metrics.hpp"
#include "trade_log.hpp"
#include "regime.hpp"
#include "../alphaflow/core/types.hpp"
#include "../alphaflow/features/feature.hpp"

namespace qts {

// ================= CONFIG =================
struct BacktestConfig {
    double periodsPerYear  = 252.0;
    double minSignalAbs    = 0.0;
    double positionLimit   = 1.0;
    bool   recordAllTicks  = false;

    RegimeThresholds regimeThr{};
};

// ================= RESULT =================
template <std::size_t MaxEquityCurvePoints = 131072,
          std::size_t MaxTradeRecords      = 65536>
struct BacktestResult {
    MetricsSnapshot summary{};

    std::array<double, MaxEquityCurvePoints> equity{};
    std::size_t equityLen = 0;

    int64_t ticksProcessed = 0;
    int64_t ordersGenerated = 0;
    int64_t fillsReceived   = 0;
};

// ================= LOB BUILDER =================
inline hft::core::LOBSnapshot to_lob(const hft::core::Tick& t) {
    hft::core::LOBSnapshot lob;

    double spread = 0.02;

    lob.best_bid = t.price - spread / 2;
    lob.best_ask = t.price + spread / 2;

    lob.bid_volume = 100 + (t.volume % 50);
    lob.ask_volume = 100 + ((t.volume * 2) % 50);

    lob.timestamp = t.timestamp;

    return lob;
}

// ================= BACKTESTER =================
template <typename Tick,
          typename FeatureEngine,
          typename StrategyAggregator,
          typename ExecutionEngine>
class Backtester {
public:
    using Result = BacktestResult<>;

    Backtester(FeatureEngine& f,
               StrategyAggregator& a,
               ExecutionEngine& e,
               BacktestConfig cfg = {}) noexcept
        : features_(f), aggregator_(a), execution_(e), cfg_(cfg) {}

    // ================= CORE =================
    void processTick(const Tick& tick) {

        ++result_.ticksProcessed;

        // ───────────── LOB ─────────────
        auto lob = to_lob(tick);

        if (lob.bid_volume == 0 || lob.ask_volume == 0)
            return;

        // ───────────── FEATURE ─────────────
        hft::features::CompactFeatureSet fs{};

        features_.update(lob, fs);

        // 🔥 CRITICAL FIX: Initialize missing fields
        fs.timestamp_ns = tick.timestamp;
        fs.microprice_delta = 0.0f;
        fs.volume_imbalance = 0.0f;


        // ───────────── VALIDATION ─────────────
        if (!std::isfinite(fs.microprice) ||
            !std::isfinite(fs.ofi) ||
            !std::isfinite(fs.microprice_z)) {
            return;
        }

        // ───────────── STRATEGY ─────────────
        auto agg = aggregator_.evaluate(fs);

        if (std::fabs(agg.final_alpha) < cfg_.minSignalAbs)
            return;


std::cout << "ALPHA=" << agg.final_alpha
          << " CONF=" << agg.total_confidence
          << " RISK=" << agg.max_risk << "\n";
        // ───────────── SIGNAL ─────────────
        hft::Signal signal{};
        signal.alpha        = agg.final_alpha;
        signal.confidence   = agg.total_confidence;
        signal.risk         = agg.max_risk;
        signal.timestamp_ns = tick.timestamp;

        // ───────────── EXECUTION ─────────────
        ++result_.ordersGenerated;

        auto report = execution_.on_signal(signal, tick.timestamp);

        if (report.filled_qty > 0) {
            ++result_.fillsReceived;
        }

        // ───────────── MARKET UPDATE ─────────────
        hft::MarketTick mt{
            lob.best_bid,
            lob.best_ask,
            (int)lob.bid_volume,
            (int)lob.ask_volume,
            50,
            tick.price,
            tick.timestamp
        };

        execution_.on_market_tick(mt);

        // ───────────── EQUITY TRACK ─────────────
        double equity = execution_.total_pnl(lob.mid_price());

        if (result_.equityLen < result_.equity.size()) {
            result_.equity[result_.equityLen++] = equity;
        }
    }

    // ================= RUN =================
    template <typename TickRange>
    void run(const TickRange& ticks) {
        std::cout << "Running backtest...\n";

        for (const auto& t : ticks) {
            processTick(t);
        }
    }

    // ================= FINALISE =================
    Result& finalise() {
        result_.summary = metrics_.snapshot(cfg_.periodsPerYear);
        return result_;
    }

private:
    FeatureEngine&      features_;
    StrategyAggregator& aggregator_;
    ExecutionEngine&    execution_;
    BacktestConfig      cfg_;

    MetricsEngine metrics_{};
    Result result_{};
};

} // namespace qts