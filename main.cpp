#include <iostream>
#include <vector>
#include <cstdio>
#include <windows.h>

// ===== ALPHAFLOW =====
#include "../alphaflow/strategy/strategy.hpp"
#include "../alphaflow/execution/order_book_sim.hpp"
#include "../alphaflow/execution/execution_engine.hpp"
#include "../alphaflow/execution/fill_model.hpp"
#include "../alphaflow/execution/impact_model.hpp"
#include "../alphaflow/backtest/backtester.hpp"
#include "../alphaflow/data/synthetic_feed.hpp"

using namespace hft;
using namespace hft::strategy;
using namespace hft::features;

int main() {

    SetConsoleOutputCP(CP_UTF8);
    std::setbuf(stdout, NULL);

    printf("STEP 1: START\n");

    // ─────────────────────────────────────────────
    // 1. Generate ticks (DATA SOURCE)
    // ─────────────────────────────────────────────
    std::vector<hft::core::Tick> ticks =
        hft::core::generateSyntheticTicks(10000);

    if (ticks.empty()) {
        printf("❌ ERROR: No ticks generated\n");
        return 1;
    }

    printf("STEP 2: ticks size = %zu\n", ticks.size());

    // ─────────────────────────────────────────────
    // 2. Feature pipeline
    // ─────────────────────────────────────────────
    hft::features::FeaturePipeline<256, 256> features;

    // ─────────────────────────────────────────────
    // 3. Strategy Aggregator
    // ─────────────────────────────────────────────
    auto agg = make_aggregator(
        MicrostructureStrategy{ MicrostructureParams{} },
        StatArbStrategy        { StatArbParams{} },
        VolumeImbalanceStrategy{ VolumeImbalanceParams{} }
    );

    // ─────────────────────────────────────────────
    // 4. Order Book + Execution Engine
    // ─────────────────────────────────────────────
    hft::OrderBookSim book;
    book.init(100.0, 500, 0.02, 0.01);

    hft::EngineConfig cfg;
    cfg.min_alpha_threshold = 0.01;   // 🔥 CRITICAL (avoid filtering everything)
    cfg.min_confidence = 0.05;

    hft::FillModel fm(0.01, 0.0002);
    hft::ImpactModel im;
    hft::ExecutionEngine engine(&book, cfg, fm, im);

    // ─────────────────────────────────────────────
    // 5. Backtester config
    // ─────────────────────────────────────────────
    qts::BacktestConfig bt_cfg;
    bt_cfg.minSignalAbs = 0.0;   // allow all signals

    // ─────────────────────────────────────────────
    // 6. Backtester (CORE PIPELINE)
    // ─────────────────────────────────────────────
    qts::Backtester<
        hft::core::Tick,
        decltype(features),
        decltype(agg),
        hft::ExecutionEngine
    > bt(features, agg, engine, bt_cfg);

    printf("STEP 3: before run\n");

    // ─────────────────────────────────────────────
    // 7. RUN BACKTEST
    // ─────────────────────────────────────────────
    bt.run(ticks);

    // printf("STEP 4: after run\n");

    // // ─────────────────────────────────────────────
    // // 8. FINALISE + RESULTS
    // // ─────────────────────────────────────────────
    // auto& result = bt.finalise();

    // printf("\n=== BACKTEST RESULT ===\n");
    // printf("Ticks processed : %lld\n", result.ticksProcessed);
    // printf("Orders generated: %lld\n", result.ordersGenerated);
    // printf("Fills received  : %lld\n", result.fillsReceived);
    // printf("Equity points   : %lld\n", result.equityCurveLen);

    // // 🔥 sanity check
    // if (result.ticksProcessed == 0) {
    //     printf("❌ ERROR: Backtester did not process ticks\n");
    // }

    // if (result.ordersGenerated == 0) {
    //     printf("⚠️ WARNING: No orders generated (check signal thresholds)\n");
    // }

    system("pause");
    return 0;
}