// normalizer.hpp
#pragma once

#include "../core/types.hpp"
#include <cmath>
#include <array>
#include <limits>

namespace hft::features {

/**
 * @class OnlineNormalizer
 * @brief Online standardization (z-score normalization) using Welford's algorithm
 * 
 * Implements streaming feature normalization:
 *   z = (x - mean) / std_dev
 * 
 * Key Features:
 * - Welford's algorithm for numerical stability (avoids catastrophic cancellation)
 * - Single pass: doesn't require storing all data
 * - Accurate mean and variance even for large numbers
 * - Handles edge cases (NaN, infinity, zero variance)
 * 
 * Welford's Algorithm:
 * Instead of computing variance as: E[X^2] - E[X]^2 (numerically unstable)
 * It uses incremental updates:
 *   mean_n = mean_{n-1} + (x_n - mean_{n-1}) / n
 *   M2_n = M2_{n-1} + (x_n - mean_{n-1}) * (x_n - mean_n)
 *   variance = M2_n / (n - 1)
 * 
 * This is MUCH more stable for streaming data.
 * 
 * Performance:
 * - Computation: ~20-30 ns per update
 * - State: 40 bytes
 * - No allocations, no transcendental functions in hot path
 * 
 * Use Case:
 * - Normalize features for ML models
 * - Scale feature values to comparable ranges
 * - Detect anomalies (unusual z-scores)
 */
class OnlineNormalizer {
public:
    OnlineNormalizer()
        : count_(0), mean_(0.0), M2_(0.0), 
          min_variance_(1e-10), last_z_(0.0) {}
    
    /**
     * @brief Add observation and return normalized value
     * 
     * Uses Welford's algorithm to update running mean and variance,
     * then returns z-score of the new observation.
     * 
     * @param x New observation value
     * @return Normalized value (z-score)
     */
    inline double update(double x) noexcept {
        // Handle invalid input
        if (!std::isfinite(x)) {
            return last_z_;  // Return last valid z-score
        }
        
        count_++;
        
        // Welford's algorithm for mean and variance
        double delta = x - mean_;
        mean_ += delta / count_;
        double delta2 = x - mean_;
        M2_ += delta * delta2;
        
        // Compute current z-score
        double variance = get_variance();
        
        if (variance < min_variance_) {
            // Near-zero variance: return 0 (normalized value)
            last_z_ = 0.0;
        } else {
            double std_dev = std::sqrt(variance);
            last_z_ = (x - mean_) / std_dev;
        }
        
        return last_z_;
    }
    
    /**
     * @brief Get current mean
     */
    inline double mean() const noexcept {
        return mean_;
    }
    
    /**
     * @brief Get current variance
     * 
     * Computed as: M2 / (n - 1) for sample variance
     * Returns 0 if fewer than 2 samples.
     */
    inline double variance() const noexcept {
        return get_variance();
    }
    
    /**
     * @brief Get current standard deviation
     */
    inline double std_dev() const noexcept {
        return std::sqrt(get_variance());
    }
    
    /**
     * @brief Get last computed z-score
     */
    inline double last_z() const noexcept {
        return last_z_;
    }
    
    /**
     * @brief Get number of observations processed
     */
    inline uint64_t count() const noexcept {
        return count_;
    }
    
    /**
     * @brief Normalize a value (without updating statistics)
     * 
     * Useful for normalizing out-of-sample data using
     * the statistics computed from training data.
     * 
     * @param x Value to normalize
     * @return Normalized value using current mean/std
     */
    inline double normalize(double x) const noexcept {
        double variance = get_variance();
        
        if (variance < min_variance_) {
            return 0.0;
        }
        
        double std_val = std::sqrt(variance);
        return (x - mean_) / std_val;
    }
    
    /**
     * @brief Denormalize a z-score back to original scale
     * 
     * @param z Normalized value (z-score)
     * @return Original scale value
     */
    inline double denormalize(double z) const noexcept {
        double std_val = std::sqrt(get_variance());
        return mean_ + z * std_val;
    }
    
    /**
     * @brief Check if variance has stabilized
     * 
     * Variance stabilizes after ~100+ samples.
     * Useful for delaying use of normalizer until sufficient data.
     * 
     * @param min_samples Minimum samples for "stabilized"
     * @return true if count >= min_samples
     */
    inline bool is_stable(uint64_t min_samples = 100) const noexcept {
        return count_ >= min_samples;
    }
    
    /**
     * @brief Reset to initial state
     */
    void reset() noexcept {
        count_ = 0;
        mean_ = 0.0;
        M2_ = 0.0;
        last_z_ = 0.0;
    }
    
    /**
     * @brief Set minimum variance threshold
     * 
     * Prevents division by very small numbers.
     * Default: 1e-10
     * 
     * @param min_var Minimum variance threshold
     */
    void set_min_variance(double min_var) noexcept {
        min_variance_ = std::abs(min_var);
    }
    
    /**
     * @brief Get detected anomaly status
     * 
     * A simple anomaly detector: flag if |z-score| > threshold
     * Typical threshold: 3.0 (99.7% of normal distribution)
     * 
     * @param threshold Z-score threshold
     * @return true if last observation is anomalous
     */
    inline bool is_anomalous(double threshold = 3.0) const noexcept {
        return std::abs(last_z_) > threshold;
    }
    
private:
    /**
     * @brief Internal variance computation
     * 
     * Returns sample variance (divides by n-1)
     */
    inline double get_variance() const noexcept {
        if (count_ < 2) return 0.0;
        return M2_ / (count_ - 1);
    }
    
    uint64_t count_;      // 8 bytes - number of observations
    double mean_;         // 8 bytes - current mean
    double M2_;           // 8 bytes - sum of squared deltas
    double min_variance_; // 8 bytes - minimum variance threshold
    double last_z_;       // 8 bytes - last z-score
};

/**
 * @class MultiFeatureNormalizer
 * @brief Normalizes multiple features simultaneously
 * 
 * Maintains independent normalizers for each feature.
 * Useful for normalizing multiple market microstructure signals.
 * 
 * Template parameter N = number of features
 */
template <uint32_t N_FEATURES = 10>
class MultiFeatureNormalizer {
public:
    /**
     * @brief Update all features
     * 
     * @param values Array of N_FEATURES feature values
     * @param output Array to store normalized values
     */
    inline void update(const std::array<double, N_FEATURES>& values,
                      std::array<double, N_FEATURES>& output) noexcept {
        for (uint32_t i = 0; i < N_FEATURES; ++i) {
            output[i] = normalizers_[i].update(values[i]);
        }
    }
    
    /**
     * @brief Normalize features (without updating statistics)
     * 
     * @param values Feature values
     * @param output Normalized values
     */
    inline void normalize(const std::array<double, N_FEATURES>& values,
                         std::array<double, N_FEATURES>& output) const noexcept {
        for (uint32_t i = 0; i < N_FEATURES; ++i) {
            output[i] = normalizers_[i].normalize(values[i]);
        }
    }
    
    /**
     * @brief Get normalizer for specific feature
     */
    inline const OnlineNormalizer& normalizer(uint32_t idx) const noexcept {
        return normalizers_[idx];
    }
    
    /**
     * @brief Get mutable normalizer for specific feature
     */
    inline OnlineNormalizer& normalizer(uint32_t idx) noexcept {
        return normalizers_[idx];
    }
    
    /**
     * @brief Check if all features are stable
     */
    inline bool all_stable(uint64_t min_samples = 100) const noexcept {
        for (uint32_t i = 0; i < N_FEATURES; ++i) {
            if (!normalizers_[i].is_stable(min_samples)) {
                return false;
            }
        }
        return true;
    }
    
    /**
     * @brief Reset all normalizers
     */
    void reset() noexcept {
        for (uint32_t i = 0; i < N_FEATURES; ++i) {
            normalizers_[i].reset();
        }
    }
    
    /**
     * @brief Number of features
     */
    static constexpr uint32_t num_features() noexcept {
        return N_FEATURES;
    }
    
private:
    std::array<OnlineNormalizer, N_FEATURES> normalizers_;
};

/**
 * @class RollingWindowNormalizer
 * @brief Normalizer using only recent samples (sliding window)
 * 
 * Instead of using all-time statistics, only considers
 * the last N samples. Adapts to regime changes better.
 * 
 * Template parameter: WINDOW_SIZE = number of recent samples
 */
template <uint32_t WINDOW_SIZE = 256>
class RollingWindowNormalizer {
public:
    RollingWindowNormalizer()
        : count_(0), pos_(0), sum_(0.0), sum_sq_(0.0) {
        values_.fill(0.0);
    }
    
    /**
     * @brief Update with new value and return z-score
     * 
     * Maintains a sliding window of recent values,
     * computing statistics only from this window.
     * 
     * @param x New observation
     * @return Normalized value (z-score within window)
     */
    inline double update(double x) noexcept {
        if (!std::isfinite(x)) return 0.0;
        
        // Remove old value if window is full
        if (count_ >= WINDOW_SIZE) {
            double old_val = values_[pos_];
            sum_ -= old_val;
            sum_sq_ -= old_val * old_val;
        }
        
        // Add new value
        values_[pos_] = x;
        sum_ += x;
        sum_sq_ += x * x;
        
        pos_ = (pos_ + 1) % WINDOW_SIZE;
        
        if (count_ < WINDOW_SIZE) {
            count_++;
        }
        
        return compute_z_score(x);
    }
    
    /**
     * @brief Get current mean from window
     */
    inline double mean() const noexcept {
        if (count_ == 0) return 0.0;
        return sum_ / count_;
    }
    
    /**
     * @brief Get current std dev from window
     */
    inline double std_dev() const noexcept {
        if (count_ < 2) return 0.0;
        
        double mean_val = mean();
        double variance = (sum_sq_ / count_) - (mean_val * mean_val);
        
        if (variance < 0.0) variance = 0.0;  // Numerical error
        return std::sqrt(variance);
    }
    
    /**
     * @brief Normalize value using current window statistics
     */
    inline double normalize(double x) const noexcept {
        double std_val = std_dev();
        if (std_val < 1e-10) return 0.0;
        return (x - mean()) / std_val;
    }
    
    /**
     * @brief Reset to initial state
     */
    void reset() noexcept {
        count_ = 0;
        pos_ = 0;
        sum_ = 0.0;
        sum_sq_ = 0.0;
        values_.fill(0.0);
    }
    
    /**
     * @brief Get number of samples in window
     */
    inline uint32_t window_count() const noexcept {
        return count_;
    }
    
    /**
     * @brief Window size
     */
    static constexpr uint32_t window_size() noexcept {
        return WINDOW_SIZE;
    }
    
private:
    inline double compute_z_score(double x) const noexcept {
        double std_val = std_dev();
        if (std_val < 1e-10) return 0.0;
        return (x - mean()) / std_val;
    }
    
    std::array<double, WINDOW_SIZE> values_;
    uint32_t count_;
    uint32_t pos_;
    double sum_;
    double sum_sq_;
};

} // namespace hft::features