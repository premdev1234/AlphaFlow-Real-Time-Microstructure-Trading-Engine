//stragegy.hpp
#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// hft::strategy  –  umbrella include
// ─────────────────────────────────────────────────────────────────────────────
//
//  Include this single header to pull in the entire strategy layer.
//
//    #include "strategy/strategy.hpp"
//
//    using namespace hft::strategy;
//    using namespace hft::features;
// ─────────────────────────────────────────────────────────────────────────────

#include "../features/feature.hpp"
#include "../strategy/microstructure_strategy.hpp"
#include "../strategy/stat_arb.hpp"
#include "../strategy/signal_aggregator.hpp"
#include "../strategy/strategy_base.hpp"
