// stat_arb.hpp
#pragma once

#include "strategy_base.hpp"
#include <cmath>
#include <array>

namespace hft::strategy {

// ─────────────────────────────────────────────────────────────────────────────
// StatArbStrategy  –  z-score mean reversion
// ─────────────────────────────────────────────────────────────────────────────
//
//  Assumes the microprice z-score reverts to zero over a medium time horizon.
//
//  Signal logic:
//    z > +entry_threshold  → SELL  (price extended above fair value)
//    z < -entry_threshold  → BUY   (price depressed below fair value)
//    |z| < dead_zone       → NEUTRAL
//
//  Alpha is a smooth function of z so we avoid cliff-edge thresholds:
//
//    raw_alpha = -tanh(steepness * z)
//
//  Confidence grows with |z| up to a saturation level.  Very extreme z
//  values are flagged with a non-zero risk_score (potential regime change).
//
//  OFI is used as a veto: if OFI strongly contradicts the mean-reversion
//  direction, confidence is dampened (trend could be real, not noise).
// ─────────────────────────────────────────────────────────────────────────────

struct StatArbParams {
    double entry_threshold   = 1.5;   ///< |z| above this activates the signal.
    double dead_zone         = 0.5;   ///< |z| below this → neutral.
    double steepness         = 0.80;  ///< tanh scale factor for alpha shaping.
    double conf_saturation_z = 3.0;   ///< |z| at which confidence saturates at 1.
    double ofi_veto_strength = 0.40;  ///< Max confidence reduction from opposing OFI.
    double extreme_z_risk    = 4.0;   ///< |z| above which risk_score starts rising.
    double risk_scale        = 0.25;  ///< Slope of risk_score above extreme_z_risk.
};

class StatArbStrategy : public StrategyBase<StatArbStrategy> {
public:
    explicit StatArbStrategy(const StatArbParams& p = {}) noexcept
        : params_(p) {}

    // ── CRTP implementation ───────────────────────────────────────────────
    [[nodiscard]]
    Signal evaluate_impl(const features::CompactFeatureSet& f) noexcept {
        const double z = f.microprice_z;

        // ── 1. Alpha ──────────────────────────────────────────────────────
        double alpha = 0.0;

        if (std::abs(z) > params_.dead_zone) {
            // Mean reversion: sell high z, buy low z → flip sign.
            alpha = -std::tanh(params_.steepness * z);
        }

        // ── 2. Confidence ─────────────────────────────────────────────────
        double conf = 0.0;

        if (std::abs(z) >= params_.entry_threshold) {
            // Linear ramp from entry_threshold to conf_saturation_z.
            const double span = params_.conf_saturation_z - params_.entry_threshold;
            const double t    = (std::abs(z) - params_.entry_threshold) / span;
            conf = clamp(t, 0.0, 1.0);
        } else if (std::abs(z) > params_.dead_zone) {
            // Partial confidence between dead_zone and entry_threshold.
            const double span = params_.entry_threshold - params_.dead_zone;
            const double t    = (std::abs(z) - params_.dead_zone) / span;
            conf = clamp(t * 0.3, 0.0, 0.3);  // cap at 30% below threshold
        }

        // OFI veto: strong order flow in the opposite direction of our mean-
        // reversion bet suggests the move may be fundamental, not noise.
        const double ofi_opposition =
            clamp(-alpha * f.ofi, 0.0, 1.0);  // positive when OFI opposes alpha

        const double veto = params_.ofi_veto_strength * ofi_opposition;
        conf = clamp(conf * (1.0 - veto), 0.0, 1.0);

        // ── 3. Risk score ─────────────────────────────────────────────────
        //   Extremely large z → potential data error or regime shift.
        double risk = 0.0;
        if (std::abs(z) > params_.extreme_z_risk) {
            risk = clamp((std::abs(z) - params_.extreme_z_risk) * params_.risk_scale,
                         0.0, 1.0);
        }

        return Signal{alpha, conf, risk};
    }

    const StatArbParams& params() const noexcept { return params_; }
    void set_params(const StatArbParams& p) noexcept { params_ = p; }

private:
    StatArbParams params_;
};

// ─────────────────────────────────────────────────────────────────────────────
// VolumeImbalanceStrategy  –  lightweight companion mean-reversion strategy
// ─────────────────────────────────────────────────────────────────────────────
//
//  Uses the bid/ask depth imbalance as a short-horizon predictor.
//  Positive imbalance (more bid depth) → mild bullish signal.
//  Can be combined in the aggregator alongside StatArbStrategy.
// ─────────────────────────────────────────────────────────────────────────────

struct VolumeImbalanceParams {
    double sensitivity  = 1.2;   ///< Scales the soft-sign of imbalance.
    double base_conf    = 0.20;  ///< Flat confidence (weaker signal overall).
};

class VolumeImbalanceStrategy : public StrategyBase<VolumeImbalanceStrategy> {
public:
    explicit VolumeImbalanceStrategy(const VolumeImbalanceParams& p = {}) noexcept
        : params_(p) {}

    [[nodiscard]]
    Signal evaluate_impl(const features::CompactFeatureSet& f) noexcept {
        const double alpha = soft_sign(f.volume_imbalance * params_.sensitivity);
        const double conf  = clamp(params_.base_conf * (1.0 + std::abs(f.volume_imbalance)),
                                   0.0, 0.50);  // max 50% – weak predictor alone
        return Signal{alpha, conf, 0.0};
    }

private:
    VolumeImbalanceParams params_;
};

} // namespace hft::strategy