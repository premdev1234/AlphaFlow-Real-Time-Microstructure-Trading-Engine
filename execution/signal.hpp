#pragma once
#include <cstdint>
#include <cmath>

namespace hft {

struct Signal {
    double  alpha;        // [-1, +1]
    double  confidence;   // [0, 1]
    double  risk;         // [0, 1]

    double  target_price;
    int     urgency;
    int64_t timestamp_ns;

    inline bool is_buy()  const noexcept { return alpha > 0.0; }
    inline bool is_sell() const noexcept { return alpha < 0.0; }
    inline bool is_flat() const noexcept { return std::fabs(alpha) < 1e-9; }

    inline double strength() const noexcept { return std::fabs(alpha); }
    inline double score() const noexcept { return strength() * confidence; }

    static Signal make(double alpha, double confidence, double risk,
                       int64_t ts_ns = 0, int urgency = 1) noexcept
    {
        return Signal{ alpha, confidence, risk, 0.0, urgency, ts_ns };
    }
};

} // namespace hft