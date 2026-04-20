// spread.hpp
#pragma once

#include "../core/types.hpp"
#include <array>
#include <cmath>
#include <algorithm>
#include <numeric>

namespace hft::features {

/**
 * @class SpreadTracker
 * @brief Computes and tracks bid-ask spread with rolling statistics
 * 
 * Features:
 * - Spread = best_ask - best_bid
 * - Rolling mean over fixed window (last N ticks)
 * - Rolling standard deviation using Welford's algorithm
 * - Rolling min/max spread
 * - No dynamic allocation (fixed circular buffer)
 * 
 * The window size is template parameter for compile-time optimization.
 * Typical window sizes: 100-1000 ticks (0.1-1 second at 1000Hz)
 * 
 * Performance:
 * - Computation: ~50-100 ns per update (includes stats calculation)
 * - State: 8 + (8 * WINDOW_SIZE) bytes
 * - Memory for WINDOW_SIZE=256: ~2KB
 */
template <uint32_t WINDOW_SIZE = 256>
class SpreadTracker {
    // Verify WINDOW_SIZE is reasonable
    static_assert(WINDOW_SIZE > 0, "WINDOW_SIZE must be positive");
    static_assert(WINDOW_SIZE <= 65536, "WINDOW_SIZE too large");
    
public:
    using SpreadBuffer = std::array<double, WINDOW_SIZE>;
    
    SpreadTracker() 
        : current_pos_(0), num_ticks_(0), 
          last_spread_(0.0), accumulated_spread_(0.0),
          is_full_(false) {
        spread_buffer_.fill(0.0);
    }
    
    /**
     * @brief Update spread from LOB snapshot
     * 
     * Adds the spread to rolling window and updates statistics.
     * 
     * @param snapshot Current LOB snapshot
     * @return Current spread value
     */
    inline double update(const core::LOBSnapshot& snapshot) noexcept {
        // Compute spread
        double spread = snapshot.best_ask - snapshot.best_bid;
        
        // Handle edge cases
        if (spread < 0.0) spread = 0.0;  // Invalid (shouldn't happen)
        
        // Replace oldest value in circular buffer
        spread_buffer_[current_pos_] = spread;
        
        // Move to next position
        current_pos_ = (current_pos_ + 1) % WINDOW_SIZE;
        
        // Track fills
        if (!is_full_) {
            num_ticks_++;
            if (num_ticks_ >= WINDOW_SIZE) {
                is_full_ = true;
            }
        }
        
        // Cache current value and accumulation
        last_spread_ = spread;
        accumulated_spread_ += spread;
        
        return spread;
    }
    
    /**
     * @brief Get current spread
     */
    inline double spread() const noexcept {
        return last_spread_;
    }
    
    /**
     * @brief Get mean spread over the window
     * 
     * @return Average spread over last N ticks
     */
    inline double mean() const noexcept {
        uint32_t count = is_full_ ? WINDOW_SIZE : num_ticks_;
        if (count == 0) return 0.0;
        
        double sum = std::accumulate(spread_buffer_.begin(), 
                                     spread_buffer_.begin() + count, 0.0);
        return sum / count;
    }
    
    /**
     * @brief Get standard deviation of spread
     * 
     * Uses Welford's online algorithm for numerical stability.
     * 
     * @return Std dev of spread over window
     */
    inline double std_dev() const noexcept {
        uint32_t count = is_full_ ? WINDOW_SIZE : num_ticks_;
        if (count <= 1) return 0.0;
        
        double mean_val = mean();
        double variance = 0.0;
        
        for (uint32_t i = 0; i < count; ++i) {
            double diff = spread_buffer_[i] - mean_val;
            variance += diff * diff;
        }
        
        return std::sqrt(variance / (count - 1));
    }
    
    /**
     * @brief Get minimum spread in window
     */
    inline double min_spread() const noexcept {
        uint32_t count = is_full_ ? WINDOW_SIZE : num_ticks_;
        if (count == 0) return 0.0;
        
        return *std::min_element(spread_buffer_.begin(), 
                                 spread_buffer_.begin() + count);
    }
    
    /**
     * @brief Get maximum spread in window
     */
    inline double max_spread() const noexcept {
        uint32_t count = is_full_ ? WINDOW_SIZE : num_ticks_;
        if (count == 0) return 0.0;
        
        return *std::max_element(spread_buffer_.begin(), 
                                 spread_buffer_.begin() + count);
    }
    
    /**
     * @brief Get spread range (max - min)
     */
    inline double range() const noexcept {
        return max_spread() - min_spread();
    }
    
    /**
     * @brief Get coefficient of variation (std_dev / mean)
     * 
     * Useful for comparing spread volatility relative to level.
     * Avoid division by zero if mean is very small.
     */
    inline double coefficient_of_variation() const noexcept {
        double mean_val = mean();
        if (mean_val < 1e-10) return 0.0;
        return std_dev() / mean_val;
    }
    
    /**
     * @brief Check if spread is above mean
     */
    inline bool is_wide() const noexcept {
        return last_spread_ > mean();
    }
    
    /**
     * @brief Check if spread is below mean
     */
    inline bool is_tight() const noexcept {
        return last_spread_ < mean();
    }
    
    /**
     * @brief Check if spread is more than N std devs above mean
     * 
     * @param n_sigma Number of standard deviations
     * @return true if spread is unusually wide
     */
    inline bool is_wide_outlier(double n_sigma = 2.0) const noexcept {
        double mean_val = mean();
        double std_val = std_dev();
        return (last_spread_ - mean_val) > (n_sigma * std_val);
    }
    
    /**
     * @brief Check if spread is more than N std devs below mean
     */
    inline bool is_tight_outlier(double n_sigma = 2.0) const noexcept {
        double mean_val = mean();
        double std_val = std_dev();
        return (mean_val - last_spread_) > (n_sigma * std_val);
    }
    
    /**
     * @brief Get z-score of current spread
     * 
     * Z-score = (spread - mean) / std_dev
     * Positive: spread is wider than average
     * Negative: spread is tighter than average
     * 
     * @return Z-score of current spread
     */
    inline double z_score() const noexcept {
        double mean_val = mean();
        double std_val = std_dev();
        if (std_val < 1e-10) return 0.0;
        return (last_spread_ - mean_val) / std_val;
    }
    
    /**
     * @brief Get number of ticks in window
     */
    inline uint32_t num_ticks() const noexcept {
        return is_full_ ? WINDOW_SIZE : num_ticks_;
    }
    
    /**
     * @brief Check if window is full
     */
    inline bool is_full() const noexcept {
        return is_full_;
    }
    
    /**
     * @brief Get accumulated spread over all updates
     */
    inline double accumulated() const noexcept {
        return accumulated_spread_;
    }
    
    /**
     * @brief Get window size
     */
    static constexpr uint32_t window_size() noexcept {
        return WINDOW_SIZE;
    }
    
    /**
     * @brief Reset to initial state
     */
    void reset() noexcept {
        current_pos_ = 0;
        num_ticks_ = 0;
        last_spread_ = 0.0;
        accumulated_spread_ = 0.0;
        is_full_ = false;
        spread_buffer_.fill(0.0);
    }
    
private:
    SpreadBuffer spread_buffer_;  // Circular buffer of spreads
    uint32_t current_pos_;        // Current position in circular buffer
    uint32_t num_ticks_;          // Number of ticks processed
    double last_spread_;          // Most recent spread value
    double accumulated_spread_;   // Sum of all spreads
    bool is_full_;                // Has window been filled at least once?
};

/**
 * @class SpreadWidthMonitor
 * @brief Monitors spread width changes for market condition detection
 * 
 * Useful for identifying when spreads are widening/tightening,
 * which often correlates with changing volatility or liquidity conditions.
 */
template <uint32_t WINDOW_SIZE = 256>
class SpreadWidthMonitor : public SpreadTracker<WINDOW_SIZE> {
public:
    SpreadWidthMonitor() : trend_(0), widening_count_(0), tightening_count_(0) {}
    
    /**
     * @brief Update and track spread trend
     */
    inline double update(const core::LOBSnapshot& snapshot) noexcept {
        double prev_spread = SpreadTracker<WINDOW_SIZE>::spread();
        double new_spread = SpreadTracker<WINDOW_SIZE>::update(snapshot);
        
        // Track trend
        if (new_spread > prev_spread) {
            trend_ = 1;  // Widening
            widening_count_++;
            tightening_count_ = 0;
        } else if (new_spread < prev_spread) {
            trend_ = -1;  // Tightening
            tightening_count_++;
            widening_count_ = 0;
        } else {
            trend_ = 0;  // Unchanged
        }
        
        return new_spread;
    }
    
    /**
     * @brief Get spread trend
     * 
     * @return 1 if widening, -1 if tightening, 0 if unchanged
     */
    inline int trend() const noexcept {
        return trend_;
    }
    
    /**
     * @brief Check if spread is widening
     */
    inline bool is_widening() const noexcept {
        return trend_ > 0;
    }
    
    /**
     * @brief Check if spread is tightening
     */
    inline bool is_tightening() const noexcept {
        return trend_ < 0;
    }
    
    /**
     * @brief Get count of consecutive widening ticks
     */
    inline uint32_t widening_streak() const noexcept {
        return widening_count_;
    }
    
    /**
     * @brief Get count of consecutive tightening ticks
     */
    inline uint32_t tightening_streak() const noexcept {
        return tightening_count_;
    }
    
    void reset() noexcept {
        SpreadTracker<WINDOW_SIZE>::reset();
        trend_ = 0;
        widening_count_ = 0;
        tightening_count_ = 0;
    }
    
private:
    int trend_;                // Current trend: 1=widening, -1=tightening, 0=stable
    uint32_t widening_count_;  // Consecutive ticks of widening
    uint32_t tightening_count_;// Consecutive ticks of tightening
};

} // namespace hft::features