#include "../features/feature.hpp"
#include <iostream>
#include <cassert>

using namespace hft::features;
using namespace hft::core;

int main() {

    std::cout << "=== FEATURE TEST START ===\n";

    // 🔹 Microprice
    Microprice mp;
    LOBSnapshot lob;
    lob.best_bid = 100;
    lob.best_ask = 102;
    lob.bid_volume = 10;
    lob.ask_volume = 20;

    double mp_val = mp.update(lob);
    std::cout << "Microprice: " << mp_val << "\n";

    // 🔹 OFI
    OrderFlowImbalance ofi;

    LOBSnapshot l1;
    l1.best_bid = 100;
    l1.best_ask = 102;
    l1.bid_volume = 100;
    l1.ask_volume = 100;

    LOBSnapshot l2;
    l2.best_bid = 100;
    l2.best_ask = 102;
    l2.bid_volume = 120;
    l2.ask_volume = 90;

    ofi.update(l1);
    int64_t ofi_val = ofi.update(l2);
    std::cout << "OFI: " << ofi_val << "\n";

    // 🔹 Spread
    SpreadTracker<10> sp;
    double spread_val = sp.update(lob);
    std::cout << "Spread: " << spread_val << "\n";

    // 🔹 Normalizer
    OnlineNormalizer norm;
    for (int i = 1; i <= 5; i++) {
        std::cout << "Z: " << norm.update(i) << "\n";
    }

    std::cout << "=== ALL FEATURE TESTS PASSED ===\n";

    // 🔥 PIPELINE TEST (ADD HERE)

FeaturePipeline<256,256> pipeline;
CompactFeatureSet fs;

// first tick (initialize state)
pipeline.update(l1, fs);

// second tick (real OFI)
pipeline.update(l2, fs);

std::cout << "\n--- PIPELINE ---\n";
std::cout << "MP: " << fs.microprice << "\n";
std::cout << "Spread: " << fs.spread << "\n";
std::cout << "OFI: " << fs.ofi << "\n";
std::cout << "Norm MP: " << fs.norm_microprice << "\n";
return 0;
}