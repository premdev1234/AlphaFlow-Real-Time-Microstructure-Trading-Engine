// strategy_base.hpp
#pragma once

#include <cstdint>
#include <optional>
#include <cmath>
#include "../features/feature.hpp"

namespace hft::strategy {

// ─────────────────────────────────────────────────────────────────────────────
// Signal
// ─────────────────────────────────────────────────────────────────────────────

/// Direction hint carried alongside a continuous alpha value.
enum class Direction : int8_t { Sell = -1, Neutral = 0, Buy = 1 };

/// Output produced by a single strategy evaluation.
/// Deliberately plain-old-data so it can live on the stack and be
/// passed by value without extra allocations.
struct Signal {
    double    alpha;       ///< Continuous signal strength in [-1, +1].
                           ///< Positive → bullish; negative → bearish.
    double    confidence;  ///< Certainty estimate in [0, 1].
    double    risk_score;  ///< Optional risk/regime flag in [0, 1].
                           ///< 0 = benign; 1 = high-risk / do-not-trade.

    Direction direction() const noexcept {
        if (alpha >  0.05) return Direction::Buy;
        if (alpha < -0.05) return Direction::Sell;
        return Direction::Neutral;
    }

    /// Weighted contribution used by the aggregator.
    double weighted_alpha() const noexcept { return alpha * confidence; }

    /// Quick validity check – catches NaN/Inf slipping through.
    bool is_valid() const noexcept {
        return std::isfinite(alpha)      &&
               std::isfinite(confidence) &&
               std::isfinite(risk_score) &&
               confidence >= 0.0 && confidence <= 1.0 &&
               risk_score  >= 0.0 && risk_score  <= 1.0;
    }

    static Signal neutral() noexcept { return {0.0, 0.0, 0.0}; }
};

// ─────────────────────────────────────────────────────────────────────────────
// StrategyBase  – CRTP interface (zero virtual dispatch overhead)
// ─────────────────────────────────────────────────────────────────────────────
//
//  Usage:
//    class MyStrategy : public StrategyBase<MyStrategy> {
//    public:
//        Signal evaluate_impl(const features::CompactFeatureSet& f) noexcept;
//    };
//
//  The base calls `evaluate_impl` through the static type, so there is NO
//  vtable lookup.  All strategy objects can still be stored in a
//  std::tuple<S1, S2, ...> and iterated at compile time.
// ─────────────────────────────────────────────────────────────────────────────

template <typename Derived>
class StrategyBase {
public:
    /// Hot-path entry point.  Dispatches to derived::evaluate_impl.
    [[nodiscard]]
    Signal evaluate(const features::CompactFeatureSet& features) noexcept {
        return static_cast<Derived*>(this)->evaluate_impl(features);
    }

    // Strategies are typically stateful (rolling windows, params) – disable copy.
    StrategyBase(const StrategyBase&)            = delete;
    StrategyBase& operator=(const StrategyBase&) = delete;
    StrategyBase(StrategyBase&&)                 = default;
    StrategyBase& operator=(StrategyBase&&)      = default;

protected:
    StrategyBase() = default;
    ~StrategyBase() = default;

    // ── Utility helpers available to all derived classes ─────────────────

    /// Clamp x to [lo, hi].
    static double clamp(double x, double lo, double hi) noexcept {
        return x < lo ? lo : (x > hi ? hi : x);
    }

    /// Smooth sigmoid that maps ℝ → (0, 1).  k controls steepness.
    static double sigmoid(double x, double k = 1.0) noexcept {
        return 1.0 / (1.0 + std::exp(-k * x));
    }

    /// Soft-sign: faster than tanh, similar shape.
    static double soft_sign(double x) noexcept {
        return x / (1.0 + std::abs(x));
    }
};

} // namespace hft::strategy