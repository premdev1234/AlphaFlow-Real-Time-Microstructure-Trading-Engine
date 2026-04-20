// ofi.hpp
#pragma once

#include "../core/types.hpp"
#include <cmath>
#include <cstdint>

namespace hft::features {

/**
 * @class OrderFlowImbalance
 * @brief Computes Order Flow Imbalance (OFI) from LOB snapshots
 * 
 * OFI measures the change in order book volumes:
 *   OFI = Δ(bid_volume) - Δ(ask_volume)
 * 
 * Interpretation:
 * - Positive OFI: More buying pressure (bid volume increased more than ask)
 * - Negative OFI: More selling pressure (ask volume increased more than bid)
 * - OFI = 0: Balanced flow
 * 
 * This is a powerful signal for short-term price prediction as it directly
 * reflects the direction and magnitude of order flow.
 * 
 * Properties:
 * - Incremental computation: Only uses current and previous state
 * - Signed quantity: Can be positive or negative
 * - Unbounded: Can accumulate over time
 * 
 * Performance:
 * - Computation: ~5-10 ns (simple arithmetic, no allocations)
 * - State: 32 bytes (2x uint64_t for volumes)
 */
class OrderFlowImbalance {
public:
    OrderFlowImbalance() 
        : prev_bid_volume_(0), prev_ask_volume_(0), 
          last_ofi_(0), accumulated_ofi_(0) {}
    
    /**
     * @brief Update OFI from new LOB snapshot
     * 
     * Computes change in volumes from previous snapshot and calculates OFI.
     * 
     * @param snapshot Current LOB snapshot
     * @return OFI value for this update (change in volume imbalance)
     */
    inline int64_t update(const core::LOBSnapshot& snapshot) noexcept {
        // Compute volume changes
        int64_t bid_change = static_cast<int64_t>(snapshot.bid_volume) - 
                            static_cast<int64_t>(prev_bid_volume_);
        int64_t ask_change = static_cast<int64_t>(snapshot.ask_volume) - 
                            static_cast<int64_t>(prev_ask_volume_);
        
        // Update previous values for next tick
        prev_bid_volume_ = snapshot.bid_volume;
        prev_ask_volume_ = snapshot.ask_volume;
        
        // OFI = change in bid - change in ask
        last_ofi_ = bid_change - ask_change;
        
        // Track accumulated OFI (optional, for trend analysis)
        accumulated_ofi_ += last_ofi_;
        
        return last_ofi_;
    }
    
    /**
     * @brief Get the last computed OFI value
     * 
     * @return OFI from most recent update
     */
    inline int64_t value() const noexcept {
        return last_ofi_;
    }
    
    /**
     * @brief Get accumulated OFI over all updates
     * 
     * Shows the net direction of order flow pressure since reset.
     * 
     * @return Sum of all OFI values
     */
    inline int64_t accumulated() const noexcept {
        return accumulated_ofi_;
    }
    
    /**
     * @brief Get OFI as a signed double for analysis
     * 
     * Useful for comparisons and statistical analysis.
     */
    inline double value_d() const noexcept {
        return static_cast<double>(last_ofi_);
    }
    
    /**
     * @brief Check if OFI is positive (buying pressure)
     */
    inline bool is_positive() const noexcept {
        return last_ofi_ > 0;
    }
    
    /**
     * @brief Check if OFI is negative (selling pressure)
     */
    inline bool is_negative() const noexcept {
        return last_ofi_ < 0;
    }
    
    /**
     * @brief Get absolute magnitude of OFI
     */
    inline uint64_t magnitude() const noexcept {
        return static_cast<uint64_t>(std::abs(last_ofi_));
    }
    
    /**
     * @brief Reset to initial state
     */
    void reset() noexcept {
        prev_bid_volume_ = 0;
        prev_ask_volume_ = 0;
        last_ofi_ = 0;
        accumulated_ofi_ = 0;
    }
    
    /**
     * @brief Get current bid volume state
     * 
     * @return Last known bid volume
     */
    inline uint64_t bid_volume() const noexcept {
        return prev_bid_volume_;
    }
    
    /**
     * @brief Get current ask volume state
     * 
     * @return Last known ask volume
     */
    inline uint64_t ask_volume() const noexcept {
        return prev_ask_volume_;
    }
    
    /**
     * @brief Get current volume imbalance (level, not change)
     * 
     * Imbalance = bid_volume - ask_volume
     * Different from OFI (which is the change).
     * 
     * @return Current volume imbalance
     */
    inline int64_t imbalance() const noexcept {
        return static_cast<int64_t>(prev_bid_volume_) - 
               static_cast<int64_t>(prev_ask_volume_);
    }
    
private:
    uint64_t prev_bid_volume_;  // 8 bytes - bid volume from previous tick
    uint64_t prev_ask_volume_;  // 8 bytes - ask volume from previous tick
    int64_t last_ofi_;          // 8 bytes - OFI from last update
    int64_t accumulated_ofi_;   // 8 bytes - sum of all OFI values
};

/**
 * @class OFIWithStats
 * @brief OFI with rolling statistics (mean, count)
 * 
 * Tracks OFI and computes simple statistics over recent updates.
 * Useful for identifying when OFI is unusually high/low.
 */
class OFIWithStats : public OrderFlowImbalance {
public:
    OFIWithStats() : ofi_count_(0), ofi_sum_(0) {}
    
    /**
     * @brief Update and track statistics
     */
    inline int64_t update(const core::LOBSnapshot& snapshot) noexcept {
        int64_t ofi = OrderFlowImbalance::update(snapshot);
        
        // Track statistics
        ofi_count_++;
        ofi_sum_ += ofi;
        
        return ofi;
    }
    
    /**
     * @brief Get average OFI over all updates
     * 
     * @return Mean OFI value
     */
    inline double mean_ofi() const noexcept {
        if (ofi_count_ == 0) return 0.0;
        return static_cast<double>(ofi_sum_) / ofi_count_;
    }
    
    /**
     * @brief Get number of updates
     */
    inline uint64_t update_count() const noexcept {
        return ofi_count_;
    }
    
    /**
     * @brief Reset statistics
     */
    void reset() noexcept {
        OrderFlowImbalance::reset();
        ofi_count_ = 0;
        ofi_sum_ = 0;
    }
    
private:
    uint64_t ofi_count_;  // Number of updates
    int64_t ofi_sum_;     // Sum of all OFI values
};

/**
 * @class OFIRatio
 * @brief OFI as a ratio of total volume change
 * 
 * Normalizes OFI by total volume for scale-invariant comparison:
 *   OFI_ratio = OFI / (|Δbid_volume| + |Δask_volume|)
 */
class OFIRatio : public OrderFlowImbalance {
public:
    /**
     * @brief Update and get OFI as ratio
     * 
     * @param snapshot Current LOB snapshot
     * @return OFI divided by total volume change
     */
    inline double update_ratio(const core::LOBSnapshot& snapshot) noexcept {
        // Get volume changes
        int64_t bid_change = static_cast<int64_t>(snapshot.bid_volume) - 
                            static_cast<int64_t>(bid_volume());
        int64_t ask_change = static_cast<int64_t>(snapshot.ask_volume) - 
                            static_cast<int64_t>(ask_volume());
        
        // Update base OFI
        int64_t ofi = OrderFlowImbalance::update(snapshot);
        
        // Compute total volume change
        int64_t total_change = std::abs(bid_change) + std::abs(ask_change);
        
        // Avoid division by zero
        if (total_change == 0) {
            return 0.0;
        }
        
        return static_cast<double>(ofi) / total_change;
    }
    
    /**
     * @brief Get last OFI ratio
     */
    inline double last_ratio() const noexcept {
        return last_ratio_;
    }
    
private:
    double last_ratio_ = 0.0;
};

} // namespace hft::features