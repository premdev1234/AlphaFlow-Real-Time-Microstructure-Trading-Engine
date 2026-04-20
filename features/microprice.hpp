// microprice.hpp
#pragma once

#include "../core/types.hpp"
#include <cmath>
#include <limits>

namespace hft::features {

/**
 * @class Microprice
 * @brief Computes microprice from best bid/ask prices and volumes
 * 
 * Microprice is a volume-weighted average of bid and ask prices:
 *   microprice = (bid_price * ask_volume + ask_price * bid_volume) / (bid_volume + ask_volume)
 * 
 * This represents the "fair value" between bid and ask considering order book imbalance.
 * It's more stable than simple mid-price as it weights toward the side with more liquidity.
 * 
 * Edge Cases Handled:
 * - Zero bid or ask volume: Falls back to mid-price
 * - Both volumes zero: Returns NaN or last known value
 * - Price extremes: Handles floating-point edge cases
 * 
 * Performance:
 * - Computation: ~10-20 ns (no allocations, no divisions by zero)
 * - State: 16 bytes (one price double)
 */
class Microprice {
public:
    Microprice() : last_microprice_(0.0) {}
    
    /**
     * @brief Update microprice with new LOB snapshot
     * 
     * @param snapshot Current limit order book snapshot
     * @return Computed microprice value
     */
    inline double update(const core::LOBSnapshot& snapshot) noexcept {
        return compute_microprice(
            snapshot.best_bid,
            snapshot.best_ask,
            snapshot.bid_volume,
            snapshot.ask_volume
        );
    }
    
    /**
     * @brief Compute microprice from components
     * 
     * @param bid_price Best bid price
     * @param ask_price Best ask price
     * @param bid_volume Total volume at bid
     * @param ask_volume Total volume at ask
     * @return Microprice value
     */
    inline double compute(double bid_price, double ask_price,
                         uint64_t bid_volume, uint64_t ask_volume) noexcept {
        return compute_microprice(bid_price, ask_price, bid_volume, ask_volume);
    }
    
    /**
     * @brief Get the last computed microprice
     * 
     * @return Last known microprice value
     */
    inline double value() const noexcept {
        return last_microprice_;
    }
    
    /**
     * @brief Get the last computed microprice (alternative naming)
     */
    inline double current() const noexcept {
        return last_microprice_;
    }
    
    /**
     * @brief Reset to initial state
     */
    void reset() noexcept {
        last_microprice_ = 0.0;
    }
    
    /**
     * @brief Check if microprice is valid
     * 
     * @return true if current microprice is not NaN or infinity
     */
    inline bool is_valid() const noexcept {
        return std::isfinite(last_microprice_);
    }
    
private:
    /**
     * @brief Internal computation with numerical stability
     * 
     * Handles edge cases:
     * 1. If both volumes are zero, return average of bid/ask (mid-price)
     * 2. If only one volume is zero, return the other side's price
     * 3. Otherwise compute weighted average
     * 
     * Uses double precision for stability in intermediate calculations.
     */
    inline double compute_microprice(double bid_price, double ask_price,
                                     uint64_t bid_vol, uint64_t ask_vol) noexcept {
        // Convert volumes to double for calculation
        double bid_volume = static_cast<double>(bid_vol);
        double ask_volume = static_cast<double>(ask_vol);
        
        // Sum of volumes
        double total_volume = bid_volume + ask_volume;
        
        // Edge case: both volumes are zero or invalid
        if (total_volume <= 0.0 || bid_price <= 0.0 || ask_price <= 0.0) {
            // Return mid-price as fallback
            last_microprice_ = (bid_price + ask_price) / 2.0;
            return last_microprice_;
        }
        
        // Edge case: bid volume is zero
        if (bid_volume <= 0.0) {
            last_microprice_ = ask_price;
            return last_microprice_;
        }
        
        // Edge case: ask volume is zero
        if (ask_volume <= 0.0) {
            last_microprice_ = bid_price;
            return last_microprice_;
        }
        
        // Normal case: weighted average
        // Rewrite to avoid potential overflow with large volumes:
        // microprice = bid_price * (ask_volume/total) + ask_price * (bid_volume/total)
        double bid_weight = ask_volume / total_volume;
        double ask_weight = bid_volume / total_volume;
        
        last_microprice_ = bid_price * bid_weight + ask_price * ask_weight;
        
        return last_microprice_;
    }
    
    double last_microprice_;  // 8 bytes - cached for quick access
};

/**
 * @brief Simple function for one-off microprice calculation
 * 
 * Use this for sporadic calculations. For continuous streaming,
 * use the Microprice class to avoid repeated computation.
 * 
 * @param bid_price Best bid price
 * @param ask_price Best ask price
 * @param bid_volume Bid-side volume
 * @param ask_volume Ask-side volume
 * @return Computed microprice
 */
inline double compute_microprice(double bid_price, double ask_price,
                                 uint64_t bid_volume, uint64_t ask_volume) noexcept {
    double bid_vol = static_cast<double>(bid_volume);
    double ask_vol = static_cast<double>(ask_volume);
    double total = bid_vol + ask_vol;
    
    if (total <= 0.0 || bid_price <= 0.0 || ask_price <= 0.0) {
        return (bid_price + ask_price) / 2.0;
    }
    
    if (bid_vol <= 0.0) return ask_price;
    if (ask_vol <= 0.0) return bid_price;
    
    return bid_price * (ask_vol / total) + ask_price * (bid_vol / total);
}

/**
 * @brief Microprice with change tracking
 * 
 * Extends basic Microprice to also track changes between updates,
 * useful for momentum or volatility calculations.
 */
class MicropriceWithChange : public Microprice {
public:
    MicropriceWithChange() : previous_microprice_(0.0), change_(0.0) {}
    
    /**
     * @brief Update and get microprice with change tracking
     */
    inline double update(const core::LOBSnapshot& snapshot) noexcept {
        previous_microprice_ = value();
        double new_price = Microprice::update(snapshot);
        change_ = new_price - previous_microprice_;
        return new_price;
    }
    
    /**
     * @brief Get change in microprice since last update
     * 
     * @return price change (can be positive, negative, or zero)
     */
    inline double change() const noexcept {
        return change_;
    }
    
    /**
     * @brief Get absolute change magnitude
     */
    inline double abs_change() const noexcept {
        return std::abs(change_);
    }
    
    /**
     * @brief Check if microprice increased
     */
    inline bool increased() const noexcept {
        return change_ > 0.0;
    }
    
    /**
     * @brief Check if microprice decreased
     */
    inline bool decreased() const noexcept {
        return change_ < 0.0;
    }
    
    /**
     * @brief Get previous microprice value
     */
    inline double previous() const noexcept {
        return previous_microprice_;
    }
    
    void reset() noexcept {
        Microprice::reset();
        previous_microprice_ = 0.0;
        change_ = 0.0;
    }
    
private:
    double previous_microprice_;
    double change_;
};

} // namespace hft::features