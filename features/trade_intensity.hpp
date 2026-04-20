// trade_intensity.hpp
#pragma once

#include "../core/types.hpp"
#include <array>
#include <algorithm>
#include <cstdint>

namespace hft::features {

/**
 * @class TradeIntensity
 * @brief Tracks number of trades within a rolling time window
 * 
 * Trade intensity (or trade rate) is the number of executed trades
 * within a fixed time window. It's a measure of market activity level.
 * 
 * Features:
 * - Efficient time-window based counting
 * - Fixed circular buffer (no dynamic allocation)
 * - Automatic eviction of old trades
 * - Multiple intensity metrics (raw count, rate, density)
 * 
 * Time Window Interpretation:
 * - A trade is "in window" if: now - trade.timestamp < window_duration
 * - Supports microsecond precision timing
 * - Adapts to different trading frequencies
 * 
 * Typical Use:
 * - Window duration: 100 ms - 1 second
 * - Max trades per window: 100-10000 depending on activity
 * - Update frequency: Every tick
 * 
 * Performance:
 * - Update: O(count_of_old_trades) typically ~10-50 ns
 * - Query: O(1) after update
 * - Memory: 8 + (8 * MAX_TRADES) bytes (negligible)
 */
template <uint32_t MAX_TRADES = 1024>
class TradeIntensity {
public:
    using TradeTimestamps = std::array<uint64_t, MAX_TRADES>;
    
    TradeIntensity(uint64_t window_duration_ns)
        : window_duration_(window_duration_ns),
          num_trades_(0),
          last_trade_count_(0),
          accumulated_trades_(0) {
        timestamps_.fill(0);
    }
    
    /**
     * @brief Record a new trade in the window
     * 
     * @param timestamp Trade timestamp in nanoseconds
     * @return Current number of trades in window
     */
    inline uint32_t add_trade(uint64_t timestamp) noexcept {
        // Prune old trades that are outside the window
        prune_old_trades(timestamp);
        
        // Add new trade if there's space
        if (num_trades_ < MAX_TRADES) {
            timestamps_[num_trades_] = timestamp;
            num_trades_++;
        }
        
        accumulated_trades_++;
        last_trade_count_ = num_trades_;
        
        return num_trades_;
    }
    
    /**
     * @brief Update window based on current time (evict old trades)
     * 
     * Call this periodically to evict trades that have aged out
     * of the time window, even if no new trades are being added.
     * 
     * @param current_time Current timestamp in nanoseconds
     * @return Current number of trades in window
     */
    inline uint32_t update_window(uint64_t current_time) noexcept {
        prune_old_trades(current_time);
        return num_trades_;
    }
    
    /**
     * @brief Get current trade count in window
     */
    inline uint32_t count() const noexcept {
        return num_trades_;
    }
    
    /**
     * @brief Get last recorded trade count
     */
    inline uint32_t last_count() const noexcept {
        return last_trade_count_;
    }
    
    /**
     * @brief Get trade intensity as frequency (trades per second)
     * 
     * Assumes window_duration is set in nanoseconds.
     * 
     * @param current_time Current timestamp in nanoseconds
     * @return Trades per second
     */
    inline double rate_per_second(uint64_t current_time) const noexcept {
        if (window_duration_ == 0) return 0.0;
        
        // Count non-expired trades
        uint32_t valid_count = 0;
        uint64_t min_time = current_time > window_duration_ ? 
                           current_time - window_duration_ : 0;
        
        for (uint32_t i = 0; i < num_trades_; ++i) {
            if (timestamps_[i] >= min_time) {
                valid_count++;
            }
        }
        
        // Convert to per-second rate
        return (valid_count * 1e9) / static_cast<double>(window_duration_);
    }
    
    /**
     * @brief Get average time between trades
     * 
     * @return Mean inter-trade time in nanoseconds
     */
    inline uint64_t mean_inter_trade_time() const noexcept {
        if (num_trades_ <= 1) return 0;
        
        uint64_t time_span = timestamps_[num_trades_ - 1] - timestamps_[0];
        return time_span / (num_trades_ - 1);
    }
    
    /**
     * @brief Get trade intensity bucket
     * 
     * Categorizes intensity into levels for strategy decisions.
     * Useful for adaptive trading strategies.
     * 
     * @param threshold Low, Medium, High thresholds (trade counts)
     * @return 0=Low, 1=Medium, 2=High, 3=VeryHigh
     */
    inline int intensity_level(uint32_t low = 10, uint32_t medium = 50, 
                               uint32_t high = 100) const noexcept {
        uint32_t count = num_trades_;
        if (count < low) return 0;
        if (count < medium) return 1;
        if (count < high) return 2;
        return 3;
    }
    
    /**
     * @brief Check if trades are active (above threshold)
     */
    inline bool is_active(uint32_t threshold = 5) const noexcept {
        return num_trades_ >= threshold;
    }
    
    /**
     * @brief Get total number of trades ever recorded
     */
    inline uint64_t total_trades() const noexcept {
        return accumulated_trades_;
    }
    
    /**
     * @brief Get window duration in nanoseconds
     */
    inline uint64_t window_duration() const noexcept {
        return window_duration_;
    }
    
    /**
     * @brief Set new window duration
     * 
     * @param duration_ns New window duration in nanoseconds
     */
    void set_window_duration(uint64_t duration_ns) noexcept {
        window_duration_ = duration_ns;
    }
    
    /**
     * @brief Reset to initial state
     */
    void reset() noexcept {
        num_trades_ = 0;
        last_trade_count_ = 0;
        timestamps_.fill(0);
    }
    
    /**
     * @brief Get maximum capacity
     */
    static constexpr uint32_t max_capacity() noexcept {
        return MAX_TRADES;
    }
    
    /**
     * @brief Check if at capacity
     */
    inline bool is_at_capacity() const noexcept {
        return num_trades_ >= MAX_TRADES;
    }
    
private:
    /**
     * @brief Remove trades that are outside the time window
     * 
     * Removes timestamps older than (current_time - window_duration_)
     * by shifting valid trades to the beginning of the array.
     */
    inline void prune_old_trades(uint64_t current_time) noexcept {
        if (num_trades_ == 0) return;
        
        // Calculate minimum valid timestamp
        uint64_t min_valid_time = current_time > window_duration_ ? 
                                 current_time - window_duration_ : 0;
        
        // Find first valid trade
        uint32_t valid_start = 0;
        while (valid_start < num_trades_ && 
               timestamps_[valid_start] < min_valid_time) {
            valid_start++;
        }
        
        // Shift valid trades to the beginning
        if (valid_start > 0 && valid_start < num_trades_) {
            std::move(timestamps_.begin() + valid_start,
                     timestamps_.begin() + num_trades_,
                     timestamps_.begin());
            num_trades_ -= valid_start;
        } else if (valid_start >= num_trades_) {
            // All trades are expired
            num_trades_ = 0;
        }
    }
    
    TradeTimestamps timestamps_;    // Circular buffer of trade timestamps
    uint64_t window_duration_;      // Time window in nanoseconds
    uint32_t num_trades_;           // Current number of trades in window
    uint32_t last_trade_count_;     // Last known trade count
    uint64_t accumulated_trades_;   // Total trades ever recorded
};

/**
 * @class TradeIntensityWithStats
 * @brief Trade intensity with rolling statistics
 * 
 * Tracks moving average and variance of trade counts
 * for detecting activity level changes.
 */
template <uint32_t MAX_TRADES = 1024, uint32_t STATS_WINDOW = 100>
class TradeIntensityWithStats : public TradeIntensity<MAX_TRADES> {
public:
    TradeIntensityWithStats(uint64_t window_duration_ns)
        : TradeIntensity<MAX_TRADES>(window_duration_ns),
          stats_pos_(0), stats_sum_(0) {
        count_history_.fill(0);
    }
    
    /**
     * @brief Update and track statistics
     */
    inline uint32_t update_window(uint64_t current_time) noexcept {
        uint32_t count = TradeIntensity<MAX_TRADES>::update_window(current_time);
        
        // Update rolling statistics
        stats_sum_ -= count_history_[stats_pos_];
        count_history_[stats_pos_] = count;
        stats_sum_ += count;
        
        stats_pos_ = (stats_pos_ + 1) % STATS_WINDOW;
        
        return count;
    }
    
    /**
     * @brief Get moving average of trade counts
     */
    inline double avg_count() const noexcept {
        return static_cast<double>(stats_sum_) / STATS_WINDOW;
    }
    
    /**
     * @brief Check if current activity is above average
     */
    inline bool above_average() const noexcept {
        return TradeIntensity<MAX_TRADES>::count() > avg_count();
    }
    
    /**
     * @brief Check if current activity is below average
     */
    inline bool below_average() const noexcept {
        return TradeIntensity<MAX_TRADES>::count() < avg_count();
    }
    
    void reset() noexcept {
        TradeIntensity<MAX_TRADES>::reset();
        stats_pos_ = 0;
        stats_sum_ = 0;
        count_history_.fill(0);
    }
    
private:
    std::array<uint32_t, STATS_WINDOW> count_history_;
    uint32_t stats_pos_;
    uint64_t stats_sum_;
};

/**
 * @brief Helper function: Create TradeIntensity for 100ms window
 * 
 * Convenience function for common use case of ~100ms window.
 */
inline TradeIntensity<1024> create_intensity_100ms() noexcept {
    const uint64_t HUNDRED_MS_NS = 100000000;  // 100 * 10^6
    return TradeIntensity<1024>(HUNDRED_MS_NS);
}

/**
 * @brief Helper function: Create TradeIntensity for 1 second window
 */
inline TradeIntensity<10000> create_intensity_1sec() noexcept {
    const uint64_t ONE_SEC_NS = 1000000000;  // 10^9
    return TradeIntensity<10000>(ONE_SEC_NS);
}

} // namespace hft::features