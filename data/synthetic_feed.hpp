#pragma once
#include <vector>
#include <random>
#include <cstdint>
#include "../core/types.hpp"   // ✅ ADD THIS

namespace hft::core {

inline std::vector<Tick> generateSyntheticTicks(int n) {

    std::vector<Tick> ticks;
    ticks.reserve(n);

    std::mt19937 rng(42);
    std::normal_distribution<double> price_move(0.0, 0.02);
    std::uniform_int_distribution<int> volume_dist(1, 100);

    double price = 100.0;
    int64_t ts = 1'700'000'000'000'000'000LL;

    for (int i = 0; i < n; ++i) {

        price += price_move(rng);
        if (price < 1.0) price = 1.0;

        Tick t;                      // ✅ FIX
        t.price = price;
        t.volume = volume_dist(rng);
        t.timestamp = ts;

        ticks.push_back(t);          // ✅ FIX

        ts += 1'000'000;
    }

    return ticks;
}

} // namespace hft::core
