// signal_aggregator.hpp
#pragma once

#include "strategy_base.hpp"
#include <array>
#include <tuple>
#include <cmath>
#include <cstddef>
#include <utility>   // std::index_sequence

namespace hft::strategy {

// ─────────────────────────────────────────────────────────────────────────────
// AggregatedSignal
// ─────────────────────────────────────────────────────────────────────────────

struct AggregatedSignal {
    double    final_alpha;      ///< Confidence-weighted mean of all strategy alphas.
    double    total_confidence; ///< Normalised combined confidence in [0, 1].
    double    agreement;        ///< Fraction of strategies agreeing on direction [0, 1].
    double    max_risk;         ///< Worst risk_score across all strategies.
    uint8_t   n_strategies;     ///< Number of strategies that contributed.

    Direction direction() const noexcept {
        if (final_alpha >  0.05) return Direction::Buy;
        if (final_alpha < -0.05) return Direction::Sell;
        return Direction::Neutral;
    }

    /// True if the signal is strong enough to consider acting on.
    bool is_actionable(double alpha_thresh  = 0.20,
                       double conf_thresh   = 0.30,
                       double risk_cap      = 0.70) const noexcept {
        return std::abs(final_alpha) >= alpha_thresh &&
               total_confidence      >= conf_thresh   &&
               max_risk              <= risk_cap;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// SignalAggregator  –  static aggregation over a compile-time set of strategies
// ─────────────────────────────────────────────────────────────────────────────
//
//  Template parameter pack Strategies must each satisfy the StrategyBase CRTP
//  contract (i.e. expose evaluate(CompactFeatureSet) → Signal).
//
//  All strategies live as data members, so the whole aggregator is stack-
//  allocatable and zero-allocation in the hot path.
//
//  Usage:
//    SignalAggregator<MicrostructureStrategy, StatArbStrategy, VolumeImbalanceStrategy>
//        agg;
//    AggregatedSignal out = agg.evaluate(features);
// ─────────────────────────────────────────────────────────────────────────────

template <typename... Strategies>
class SignalAggregator {
    static constexpr std::size_t N = sizeof...(Strategies);
    static_assert(N >= 1, "Need at least one strategy");
    static_assert(N <= 16, "More than 16 strategies – reconsider the design");

public:
    // Allow construction with per-strategy parameter packs forwarded.
    template <typename... Args>
    explicit SignalAggregator(Args&&... args)
        : strategies_(std::forward<Args>(args)...) {}

    SignalAggregator() = default;

    // ── Hot path ──────────────────────────────────────────────────────────

    [[nodiscard]]
    AggregatedSignal evaluate(const features::CompactFeatureSet& f) noexcept {
        // Collect signals from all strategies – NO dynamic allocation.
        std::array<Signal, N> signals;
        collect_signals(f, signals, std::index_sequence_for<Strategies...>{});
        return aggregate(signals);
    }

    // ── Accessors for strategy-level tuning ───────────────────────────────

    template <std::size_t I>
    auto& get_strategy() noexcept { return std::get<I>(strategies_); }

    template <std::size_t I>
    const auto& get_strategy() const noexcept { return std::get<I>(strategies_); }

private:
    std::tuple<Strategies...> strategies_;

    // ── Compile-time strategy dispatch ───────────────────────────────────
    template <std::size_t... Is>
    void collect_signals(const features::CompactFeatureSet& f,
                         std::array<Signal, N>& out,
                         std::index_sequence<Is...>) noexcept {
        // Pack expansion: calls evaluate on each strategy in index order.
        ((out[Is] = std::get<Is>(strategies_).evaluate(f)), ...);
    }

    // ── Aggregation math ─────────────────────────────────────────────────
    static AggregatedSignal aggregate(const std::array<Signal, N>& sigs) noexcept {
        double sum_wa   = 0.0;   // Σ alpha_i * confidence_i
        double sum_c    = 0.0;   // Σ confidence_i
        double max_risk = 0.0;
        int    agree    = 0;     // strategies whose alpha matches final direction

        // First pass: compute weighted alpha and total confidence.
        for (const auto& s : sigs) {
            if (!s.is_valid()) continue;
            sum_wa   += s.weighted_alpha();
            sum_c    += s.confidence;
            if (s.risk_score > max_risk) max_risk = s.risk_score;
        }

        const double final_alpha = (sum_c > 1e-12) ? sum_wa / sum_c : 0.0;
        const Direction final_dir =
            (final_alpha >  0.05) ? Direction::Buy  :
            (final_alpha < -0.05) ? Direction::Sell : Direction::Neutral;

        // Second pass: count agreements.
        for (const auto& s : sigs) {
            if (s.direction() == final_dir) ++agree;
        }

        const double agreement = static_cast<double>(agree) / N;

        // Normalise total_confidence: scale by agreement to penalise disagreement.
        // This prevents a 50/50 split from looking artificially confident.
        const double avg_conf        = sum_c / N;
        const double total_confidence = avg_conf * (0.5 + 0.5 * agreement);

        return AggregatedSignal{
            final_alpha,
            total_confidence,
            agreement,
            max_risk,
            static_cast<uint8_t>(N)
        };
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// make_aggregator  –  deduction helper (C++17)
// ─────────────────────────────────────────────────────────────────────────────
//
//  auto agg = make_aggregator(MicrostructureStrategy{}, StatArbStrategy{});
// ─────────────────────────────────────────────────────────────────────────────

template <typename... Strategies>
auto make_aggregator(Strategies&&... strats) {
    return SignalAggregator<std::decay_t<Strategies>...>(
        std::forward<Strategies>(strats)...);
}

} // namespace hft::strategy