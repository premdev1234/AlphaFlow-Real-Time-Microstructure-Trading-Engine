// impact_model.hpp
#pragma once
// ============================================================
//  impact_model.hpp  –  HFT Execution Module
//  Market impact model: estimates how our order moves prices.
//
//  Model family: Square-Root Impact (industry standard):
//    impact = eta * sigma * sqrt(Q / V_daily)
//  where:
//    eta     – empirical constant (~0.1–0.4)
//    sigma   – daily volatility
//    Q       – our order quantity
//    V_daily – expected daily volume
//
//  Also provides a simpler linear model for low-latency use:
//    impact = k * (Q / L)
//  where L = available liquidity at that level.
//
//  Both are zero-allocation, no branches on hot path.
// ============================================================

#include <cmath>
#include <algorithm>
#include "order.hpp"

namespace hft {

// ─── ImpactParams: calibration knobs ────────────────────────
struct ImpactParams {
    // Square-root model
    double eta          = 0.15;    // empirical impact constant
    double sigma        = 0.001;   // intraday volatility per unit time
    double daily_volume = 1e6;     // expected daily volume

    // Linear model (fallback / fast path)
    double linear_k     = 0.0005;  // linear coefficient

    // Decay: impact diminishes as order is filled (convexity)
    double decay_alpha  = 0.5;     // exponent on fill_ratio

    // Cap on maximum impact (in price units)
    double max_impact   = 0.05;
};

// ─── ImpactModel ─────────────────────────────────────────────
struct ImpactModel {
    ImpactParams params;

    explicit ImpactModel(ImpactParams p = {}) : params(p) {}

    // ── Estimate impact BEFORE the order is placed ───────────
    //  Returns a price adjustment (positive = worse for buyer)
    //  `order_qty`   – size of our order
    //  `liquidity`   – depth at the relevant price level
    //  `side`        – BUY or SELL
    inline double estimate_impact(int    order_qty,
                                  int    liquidity,
                                  Side   side) const noexcept
    {
        double qty = static_cast<double>(order_qty);
        double liq = static_cast<double>(std::max(liquidity, 1));

        // Square-root model (more realistic for larger orders)
        double sqrt_impact = params.eta
                           * params.sigma
                           * std::sqrt(qty / params.daily_volume);

        // Linear component (dominant for small orders)
        double linear_impact = params.linear_k * (qty / liq);

        // Blend: use square-root for large orders, linear for small
        double blend = std::min(qty / (0.01 * params.daily_volume), 1.0);
        double raw_impact = (1.0 - blend) * linear_impact
                          + blend        * sqrt_impact;

        // Cap to prevent unrealistic values
        raw_impact = std::min(raw_impact, params.max_impact);

        // BUY: price goes up; SELL: price goes down (worse for us)
        return (side == Side::BUY) ? +raw_impact : -raw_impact;
    }

    // ── Adjust execution price for impact ────────────────────
    //  Call before submitting; returned price is expected fill.
    inline double adjusted_price(double base_price,
                                 int    order_qty,
                                 int    liquidity,
                                 Side   side) const noexcept
    {
        return base_price + estimate_impact(order_qty, liquidity, side);
    }

    // ── Incremental impact during partial fills ───────────────
    //  As the order fills, remaining impact on remaining qty.
    //  Uses a concave decay: impact per unit decreases as filled.
    inline double incremental_impact(int    total_qty,
                                     int    remaining_qty,
                                     int    liquidity,
                                     Side   side) const noexcept
    {
        if (remaining_qty <= 0) return 0.0;
        double filled_frac  = 1.0 - static_cast<double>(remaining_qty)
                                        / total_qty;
        // Decay factor: impact wanes as earlier fills consumed liquidity
        double decay        = std::pow(1.0 - filled_frac, params.decay_alpha);
        double full_impact  = estimate_impact(total_qty, liquidity, side);
        return full_impact * decay;
    }

    // ── Permanent vs Temporary impact split ──────────────────
    //  In practice ~50% of impact is permanent (information signal),
    //  ~50% temporary (mechanical pressure that reverts).
    struct ImpactComponents {
        double permanent;   // doesn't revert
        double temporary;   // reverts over time
        double total;
    };

    inline ImpactComponents decompose_impact(int  order_qty,
                                             int  liquidity,
                                             Side side) const noexcept
    {
        double total = estimate_impact(order_qty, liquidity, side);
        ImpactComponents ic;
        ic.total     = total;
        ic.permanent = total * 0.5;
        ic.temporary = total * 0.5;
        return ic;
    }

    // ── Reversion estimate ───────────────────────────────────
    //  Price mean-reverts by `temporary_impact` after order completes.
    //  `ticks_elapsed` – time since order completion
    //  `reversion_halflife_ticks` – half-life of temp impact decay
    inline double reversion_at(double temporary_impact,
                                int    ticks_elapsed,
                                int    reversion_halflife_ticks = 10) const noexcept
    {
        if (reversion_halflife_ticks <= 0) return 0.0;
        double t = static_cast<double>(ticks_elapsed);
        double h = static_cast<double>(reversion_halflife_ticks);
        // Exponential decay: reverted = temp * (1 - exp(-t * ln2 / h))
        double reverted_frac = 1.0 - std::exp(-t * 0.693147 / h);
        return temporary_impact * reverted_frac;
    }
};

} // namespace hft