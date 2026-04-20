//feature.hpp
#pragma once



#include "../features/spread.hpp"
#include "../features/trade_intensity.hpp"
#include "../features/normalizer.hpp"
#include "../features/microprice.hpp"
#include "../features/ofi.hpp"


#include <cstdint>

namespace hft::features {

struct CompactFeatureSet {

    // ── Core price features ─────────────────────────
    double microprice;          // 8 bytes
    double microprice_z;        // 8 bytes - REQUIRED (stat arb uses this)

    // ── Order flow ──────────────────────────────────
    double ofi;                 // 8 bytes - MUST be double (not int64_t)

    // ── Meta ────────────────────────────────────────
    uint64_t timestamp_ns;      // 8 bytes

    // ── Normalized (float) ──────────────────────────
    float spread;               // 4 bytes
    float spread_normalized;    // 4 bytes
    float ofi_short;            // 4 bytes
    float ofi_long;             // 4 bytes

    // ── Derived (float) ─────────────────────────────
    float microprice_delta;     // 4 bytes
    float volume_imbalance;     // 4 bytes

    // ── Meta (compact) ──────────────────────────────
    uint32_t trade_count;       // 4 bytes
    uint32_t sequence;          // 4 bytes - for tracking order of features
};

static_assert(sizeof(CompactFeatureSet) <= 64, "CompactFeatureSet should fit in one cache line");


template <uint32_t SPREAD_WINDOW = 256, uint32_t OFI_LOOKBACK = 256>
class FeaturePipeline {
public:
    FeaturePipeline()
    : microprice_(),
      ofi_(),
      spreads_(),
      trade_intensity_(100000000),  
      norm_mp_(),
      norm_ofi_(),
      norm_spread_() {}
    
    /**
     * @brief Update all features from LOB snapshot
     * 
     * @param snapshot Current LOB snapshot
     * @param output Computed feature set
     */
    inline void update(const core::LOBSnapshot& snapshot, 
                   CompactFeatureSet& output) noexcept {

    // ===== RAW FEATURES =====
    output.microprice = microprice_.update(snapshot);
    output.spread     = spreads_.update(snapshot);

    // OFI (safe cast)
    double ofi_val = static_cast<double>(ofi_.update(snapshot));

    // 🔥 CLAMP OFI (prevents explosion)
    if (ofi_val > 1e6) ofi_val = 1e6;
    if (ofi_val < -1e6) ofi_val = -1e6;

    output.ofi = ofi_val;

    // ===== NORMALIZATION (SAFE) =====

    double z_mp = norm_mp_.update(output.microprice);
    output.microprice_z = std::isfinite(z_mp) ? z_mp : 0.0;

    double z_sp = norm_spread_.update(output.spread);
    output.spread_normalized = std::isfinite(z_sp) ? z_sp : 0.0;

    double z_ofi = norm_ofi_.update(output.ofi);
    output.ofi = std::isfinite(z_ofi) ? z_ofi : 0.0;
}
    
    /**
     * @brief Update trades in the intensity tracker
     * 
     * Call this in addition to update() when trades occur.
     * 
     * @param timestamp Trade timestamp
     */
    inline void add_trade(uint64_t timestamp) noexcept {
        trade_intensity_.add_trade(timestamp);
    }
    
    /**
     * @brief Get current feature snapshot
     */
    inline const Microprice& microprice() const noexcept {
        return microprice_;
    }
    
    inline const OrderFlowImbalance& ofi() const noexcept {
        return ofi_;
    }
    
    inline const SpreadTracker<SPREAD_WINDOW>& spreads() const noexcept {
        return spreads_;
    }
    
    inline const TradeIntensity<1024>& trade_intensity() const noexcept {
        return trade_intensity_;
    }
    
    /**
     * @brief Reset all features
     */
    void reset() noexcept {
        microprice_.reset();
        ofi_.reset();
        spreads_.reset();
        trade_intensity_.reset();
        norm_mp_.reset();
        norm_ofi_.reset();
        norm_spread_.reset();
    }
    
private:
    Microprice microprice_;
    OrderFlowImbalance ofi_;
    SpreadTracker<SPREAD_WINDOW> spreads_;
    TradeIntensity<1024> trade_intensity_;
    
    OnlineNormalizer norm_mp_;
    OnlineNormalizer norm_ofi_;
    OnlineNormalizer norm_spread_;
};

/**
 * @brief Create a standard features pipeline with typical settings
 * 
 * Convenience function for common use case.
 */
inline FeaturePipeline<256, 256> create_standard_pipeline() noexcept {
    return FeaturePipeline<256, 256>();
}

} // namespace hft::features