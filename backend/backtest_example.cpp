#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <random>
#include <iostream>
#include <memory>

// Backtest
#include "../backtest/metrics.hpp"
#include "../backtest/trade_log.hpp"
#include "../backtest/regime.hpp"
#include "../backtest/backtester.hpp"
#include "../backtest/walk_forward.hpp"

// HFT system
#include "../strategy/strategy.hpp"
#include "../features/feature.hpp"
#include "../execution/order_book_sim.hpp"
#include "../execution/execution_engine.hpp"
#include "../execution/fill_model.hpp"
#include "../execution/impact_model.hpp"

#include "../features/order.hpp"
#include "../features/feature.hpp"
#include "../execution/signal.hpp"
#include "../execution/order_book_sim.hpp"


using namespace hft;
using namespace hft::features;
using namespace hft::strategy;

// ====================== SIGNAL BRIDGE ======================
hft::Signal to_exec_signal(const hft::strategy::Signal& s, int64_t ts) {
    hft::Signal out{};
    out.alpha = s.alpha;
    out.confidence = s.confidence;
    out.risk = s.risk_score;
    out.timestamp_ns = ts;
    return out;
}

// ====================== TICK ======================
namespace qts {
struct Tick {
    int64_t timestamp = 0;
    double bid = 0;
    double ask = 0;
    double last = 0;
    double volume = 0;
};
}

// ====================== LOB CONVERTER ======================
hft::core::LOBSnapshot to_lob(const qts::Tick& t) {
    hft::core::LOBSnapshot lob{};
    
    lob.best_bid = t.bid;
    lob.best_ask = t.ask;
    lob.bid_volume = 100;
    lob.ask_volume = 100;

    return lob;
}

// ====================== DATA ======================
std::vector<qts::Tick> generateSyntheticTicks(std::size_t n) {
    std::mt19937_64 rng(42);
    std::normal_distribution<double> noise(0.0, 0.01);

    std::vector<qts::Tick> ticks;
    double mid = 100.0;

    for (size_t i = 0; i < n; i++) {
        mid += noise(rng);

        ticks.push_back({
            (int64_t)i * 1000000,
            mid - 0.01,
            mid + 0.01,
            mid,
            100
        });
    }
    return ticks;
}

// ====================== MAIN ======================
int main() {
    std::cout << "START in backtest_example.cpp \n";

    // DATA
    auto ticks = generateSyntheticTicks(5000);

    // ====================== REAL SYSTEM ======================
    
    FeaturePipeline<10,50> features;

    auto aggregator = make_aggregator(
        MicrostructureStrategy{},
        StatArbStrategy{},
        VolumeImbalanceStrategy{}
    );

    OrderBookSim book;
    book.init(100.0, 500, 0.02, 0.01);

    ExecutionEngine execution(&book, EngineConfig{}, FillModel{}, ImpactModel{});

    // ====================== BACKTEST ======================

    qts::BacktestConfig cfg{};
    cfg.minSignalAbs = 0.0;

    using BT = qts::Backtester<
        qts::Tick,
        decltype(features),
        decltype(aggregator),
        decltype(execution),
        4096,
        4096
    >;

    auto bt = std::make_unique<BT>(features, aggregator, execution, cfg);

    // ====================== LOOP ======================
  bt->run(ticks);
auto& result = bt->finalise();


    std::cout << "Trades: " << result.summary.totalTrades << "\n";
    std::cout << "PnL: " << result.summary.totalPnl << "\n";

    // ====================== WALK FORWARD ======================
    std::cout << "\nRunning WF...\n";

    using WF = qts::WalkForward<
        qts::Tick,
        decltype(features),
        decltype(aggregator),
        decltype(execution),
        8
    >;

    qts::WalkForwardConfig wfCfg{};
    wfCfg.trainSize = 500;
    wfCfg.testSize = 200;
    wfCfg.stepSize = 200;

    auto wf = std::make_unique<WF>(features, aggregator, execution, wfCfg, cfg);

    size_t n = wf->run(ticks.data(), ticks.size());

    std::cout << "Windows: " << n << "\n";

    return 0;
}