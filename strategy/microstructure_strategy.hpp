#pragma once

#include "../strategy/strategy_base.hpp"
#include <cmath>
#include <array>
#include <numeric>

namespace hft::strategy {

// ─────────────────────────────────────────────────────────────────────────────
// MicrostructureStrategy
// ─────────────────────────────────────────────────────────────────────────────
//
//  Exploits short-horizon order-flow & price signals:
//
//    alpha = tanh(ofi_weight * OFI + price_weight * microprice_delta_norm)
//
//  Confidence is reduced when the spread is wide (signal less reliable)
//  and boosted when OFI magnitude is high (strong order-flow conviction).
//
//  No dynamic allocation.  The rolling microprice buffer is a fixed-size
//  ring array on the strategy object itself.
// ─────────────────────────────────────────────────────────────────────────────

struct MicrostructureParams {
    double ofi_weight         = 0.65;  ///< Blend weight for OFI contribution.
    double price_weight       = 0.35;  ///< Blend weight for microprice delta.
    double spread_penalty_cap = 0.50;  ///< Max confidence reduction from wide spread.
    double spread_threshold   = 2.0;   ///< spread_norm above which penalty starts.
    double ofi_boost_scale    = 0.30;  ///< How much strong OFI boosts confidence.
    double min_confidence     = 0.05;  ///< Floor so neutral features give near-0 conf.
    double risk_spread_scale  = 0.40;  ///< Wide spread → higher risk score.
};

class MicrostructureStrategy : public StrategyBase<MicrostructureStrategy> {
public:
    explicit MicrostructureStrategy(const MicrostructureParams& p = {}) noexcept
        : params_(p) {}

    // ── CRTP implementation ───────────────────────────────────────────────
    [[nodiscard]]
    Signal evaluate_impl(const features::CompactFeatureSet& f) noexcept {
        // ── 1. Alpha ──────────────────────────────────────────────────────
        //   OFI: already in [-1, +1].  Positive ↔ more aggressive buy flow.
        //   microprice_delta: signed price momentum (pre-normalised by feature layer).
        //
        //   We blend both then pass through tanh to keep output in (-1, +1).
        const double raw_alpha =
            params_.ofi_weight   * f.ofi +
            params_.price_weight * clamp(f.microprice_delta * 200.0, -1.0, 1.0);

        const double alpha = std::tanh(raw_alpha * 1.5);

        // ── 2. Confidence ─────────────────────────────────────────────────
        //   Base confidence from OFI magnitude.
        double conf = params_.min_confidence +
                      (1.0 - params_.min_confidence) * std::abs(f.ofi);

        //   Spread penalty: wider spread → less reliable signal.
        if (f.spread_normalized > params_.spread_threshold) {
            const double excess   = f.spread_normalized - params_.spread_threshold;
            const double penalty  = clamp(excess * 0.25, 0.0, params_.spread_penalty_cap);
            conf *= (1.0 - penalty);
        }

        //   OFI conviction boost: very strong order-flow raises confidence.
        const double ofi_mag = std::abs(f.ofi);
        if (ofi_mag > 0.7) {
            conf = clamp(conf + params_.ofi_boost_scale * (ofi_mag - 0.7), 0.0, 1.0);
        }

        conf = clamp(conf, 0.0, 1.0);

        // ── 3. Risk score ─────────────────────────────────────────────────
        //   Wide spread → harder to execute, higher slippage risk.
        const double risk = clamp(f.spread_normalized * params_.risk_spread_scale, 0.0, 1.0);

        return Signal{alpha, conf, risk};
    }

    const MicrostructureParams& params() const noexcept { return params_; }
    void set_params(const MicrostructureParams& p) noexcept { params_ = p; }

private:
    MicrostructureParams params_;
};

} // namespace hft::strategy