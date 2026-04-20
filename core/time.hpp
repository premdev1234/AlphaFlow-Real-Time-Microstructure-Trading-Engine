// time.hpp
#pragma once

#include <chrono>
#include <cstdint>
#include <ratio>
#include <iomanip>
#include <sstream>
#include <string>
#include <thread>
namespace hft::core {

// ============================================================================
// Time Type Aliases and Constants
// ============================================================================

// Timestamp in nanoseconds since epoch
using Timestamp = uint64_t;
using Duration = int64_t;  // Can be negative for time differences

// Constants for time conversions
constexpr uint64_t NS_PER_US = 1000;
constexpr uint64_t NS_PER_MS = 1000000;
constexpr uint64_t NS_PER_SEC = 1000000000;
constexpr uint64_t US_PER_MS = 1000;
constexpr uint64_t US_PER_SEC = 1000000;
constexpr uint64_t MS_PER_SEC = 1000;

// ============================================================================
// High-Resolution Clock Wrapper
// ============================================================================

/**
 * @class HighResolutionClock
 * @brief Provides nanosecond precision timestamps
 * 
 * Wrapper around std::chrono::high_resolution_clock for consistency
 * and efficient timestamp acquisition.
 * 
 * Key Points:
 * - Uses steady_clock internally for monotonic time
 * - Provides nanosecond precision
 * - Minimal overhead (~20-30 ns on modern x86)
 * - Can be called frequently without performance penalty
 * 
 * Usage:
 * @code
 * uint64_t now_ns = HighResolutionClock::now();  // Nanoseconds since epoch
 * uint64_t now_us = HighResolutionClock::now_us();  // Microseconds
 * @endcode
 */
class HighResolutionClock {
public:
    /**
     * @brief Get current time in nanoseconds
     * 
     * @return Current timestamp in nanoseconds since epoch
     */
    static inline Timestamp now() noexcept {
        auto tp = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            tp.time_since_epoch()).count();
    }
    
    /**
     * @brief Get current time in microseconds
     * 
     * @return Current timestamp in microseconds since epoch
     */
    static inline Timestamp now_us() noexcept {
        auto tp = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::microseconds>(
            tp.time_since_epoch()).count();
    }
    
    /**
     * @brief Get current time in milliseconds
     * 
     * @return Current timestamp in milliseconds since epoch
     */
    static inline Timestamp now_ms() noexcept {
        auto tp = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            tp.time_since_epoch()).count();
    }
    
    /**
     * @brief Get current time in seconds
     * 
     * @return Current timestamp in seconds since epoch
     */
    static inline Timestamp now_sec() noexcept {
        auto tp = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::seconds>(
            tp.time_since_epoch()).count();
    }
    
    /**
     * @brief Get underlying steady_clock time point
     */
    static inline auto now_tp() noexcept {
        return std::chrono::steady_clock::now();
    }
};

// ============================================================================
// Time Conversion Utilities
// ============================================================================

/**
 * @brief Convert nanoseconds to microseconds
 */
inline constexpr uint64_t ns_to_us(uint64_t ns) noexcept {
    return ns / NS_PER_US;
}

/**
 * @brief Convert nanoseconds to milliseconds
 */
inline constexpr uint64_t ns_to_ms(uint64_t ns) noexcept {
    return ns / NS_PER_MS;
}

/**
 * @brief Convert nanoseconds to seconds
 */
inline constexpr double ns_to_sec(uint64_t ns) noexcept {
    return static_cast<double>(ns) / NS_PER_SEC;
}

/**
 * @brief Convert microseconds to nanoseconds
 */
inline constexpr uint64_t us_to_ns(uint64_t us) noexcept {
    return us * NS_PER_US;
}

/**
 * @brief Convert microseconds to milliseconds
 */
inline constexpr uint64_t us_to_ms(uint64_t us) noexcept {
    return us / US_PER_MS;
}

/**
 * @brief Convert microseconds to seconds
 */
inline constexpr double us_to_sec(uint64_t us) noexcept {
    return static_cast<double>(us) / US_PER_SEC;
}

/**
 * @brief Convert milliseconds to nanoseconds
 */
inline constexpr uint64_t ms_to_ns(uint64_t ms) noexcept {
    return ms * NS_PER_MS;
}

/**
 * @brief Convert milliseconds to microseconds
 */
inline constexpr uint64_t ms_to_us(uint64_t ms) noexcept {
    return ms * US_PER_MS;
}

/**
 * @brief Convert milliseconds to seconds
 */
inline constexpr double ms_to_sec(uint64_t ms) noexcept {
    return static_cast<double>(ms) / MS_PER_SEC;
}

/**
 * @brief Convert seconds to nanoseconds
 */
inline constexpr uint64_t sec_to_ns(uint64_t sec) noexcept {
    return sec * NS_PER_SEC;
}

/**
 * @brief Convert seconds to microseconds
 */
inline constexpr uint64_t sec_to_us(uint64_t sec) noexcept {
    return sec * US_PER_SEC;
}

/**
 * @brief Convert seconds to milliseconds
 */
inline constexpr uint64_t sec_to_ms(uint64_t sec) noexcept {
    return sec * MS_PER_SEC;
}

// ============================================================================
// Latency Measurement Utilities
// ============================================================================

/**
 * @class LatencyStats
 * @brief Accumulates latency statistics for performance monitoring
 * 
 * Tracks minimum, maximum, and average latency over a measurement period.
 * Used for real-time monitoring of system performance.
 * 
 * Note: This is intended for post-processing analysis, not to be used
 * on the critical latency path itself.
 */
class LatencyStats {
public:
    LatencyStats() 
        : min_ns_(UINT64_MAX), max_ns_(0), sum_ns_(0), count_(0) {}
    
    /**
     * @brief Record a latency measurement in nanoseconds
     */
    void record_ns(uint64_t latency_ns) noexcept {
        if (latency_ns < min_ns_) min_ns_ = latency_ns;
        if (latency_ns > max_ns_) max_ns_ = latency_ns;
        sum_ns_ += latency_ns;
        count_++;
    }
    
    /**
     * @brief Record a latency measurement in microseconds
     */
    void record_us(uint64_t latency_us) noexcept {
        record_ns(latency_us * NS_PER_US);
    }
    
    /**
     * @brief Get minimum latency in nanoseconds
     */
    uint64_t min_ns() const noexcept {
        return count_ > 0 ? min_ns_ : 0;
    }
    
    /**
     * @brief Get maximum latency in nanoseconds
     */
    uint64_t max_ns() const noexcept {
        return max_ns_;
    }
    
    /**
     * @brief Get average latency in nanoseconds
     */
    uint64_t avg_ns() const noexcept {
        return count_ > 0 ? sum_ns_ / count_ : 0;
    }
    
    /**
     * @brief Get minimum latency in microseconds
     */
    uint64_t min_us() const noexcept {
        return ns_to_us(min_ns());
    }
    
    /**
     * @brief Get maximum latency in microseconds
     */
    uint64_t max_us() const noexcept {
        return ns_to_us(max_ns());
    }
    
    /**
     * @brief Get average latency in microseconds
     */
    uint64_t avg_us() const noexcept {
        return ns_to_us(avg_ns());
    }
    
    /**
     * @brief Get number of measurements
     */
    uint64_t count() const noexcept {
        return count_;
    }
    
    /**
     * @brief Reset statistics
     */
    void reset() noexcept {
        min_ns_ = UINT64_MAX;
        max_ns_ = 0;
        sum_ns_ = 0;
        count_ = 0;
    }
    
    /**
     * @brief Get human-readable statistics string
     */
    std::string summary() const {
        std::ostringstream oss;
        oss << "LatencyStats{count=" << count_
            << ", min=" << min_us() << "us"
            << ", max=" << max_us() << "us"
            << ", avg=" << avg_us() << "us}";
        return oss.str();
    }
    
private:
    uint64_t min_ns_;
    uint64_t max_ns_;
    uint64_t sum_ns_;
    uint64_t count_;
};

// ============================================================================
// RAII-Based Latency Measurement
// ============================================================================

/**
 * @class LatencyMeasurement
 * @brief RAII timer for measuring latency of code blocks
 * 
 * Captures start time on construction and measures elapsed time on destruction.
 * Records measurement to provided LatencyStats object.
 * 
 * Usage:
 * @code
 * {
 *     LatencyStats stats;
 *     {
 *         LatencyMeasurement timer(stats);
 *         // ... code to measure ...
 *     }  // Measurement recorded automatically
 *     std::cout << stats.summary() << std::endl;
 * }
 * @endcode
 * 
 * Alternative (for continuous measurement):
 * @code
 * LatencyStats stats;
 * while (true) {
 *     {
 *         LatencyMeasurement timer(stats);
 *         process_tick();
 *     }
 *     if (iterations % 1000 == 0) {
 *         std::cout << "Per-tick latency: " << stats.avg_us() << " µs\n";
 *     }
 * }
 * @endcode
 */
class LatencyMeasurement {
public:
    explicit LatencyMeasurement(LatencyStats& stats) 
        : stats_(stats), start_(HighResolutionClock::now()) {}
    
    ~LatencyMeasurement() {
        auto elapsed = HighResolutionClock::now() - start_;
        stats_.record_ns(elapsed);
    }
    
    /**
     * @brief Get elapsed time so far (in nanoseconds)
     * 
     * Note: This doesn't record the measurement, just reports current elapsed.
     */
    uint64_t elapsed_ns() const noexcept {
        return HighResolutionClock::now() - start_;
    }
    
    /**
     * @brief Get elapsed time so far (in microseconds)
     */
    uint64_t elapsed_us() const noexcept {
        return ns_to_us(elapsed_ns());
    }
    
private:
    LatencyStats& stats_;
    Timestamp start_;
};

// ============================================================================
// Busy-Wait Utilities (for Ultra-Low-Latency Spinning)
// ============================================================================

/**
 * @brief Busy-wait until a specific time
 * 
 * Spins on high-resolution timer until target_ns is reached.
 * Use with caution - wastes CPU cycles but provides maximum precision.
 * Suitable for ultra-low-latency systems where precision is critical.
 * 
 * @param target_ns The target nanosecond timestamp to wait until
 * 
 * Note: If target_ns is in the past, this returns immediately.
 */
inline void spin_wait_until(Timestamp target_ns) noexcept {
    while (HighResolutionClock::now() < target_ns) {
        // Busy wait - no CPU yield
    }
}

/**
 * @brief Busy-wait for a specific duration
 * 
 * @param duration_ns Duration to wait in nanoseconds
 */
inline void spin_wait(uint64_t duration_ns) noexcept {
    auto target = HighResolutionClock::now() + duration_ns;
    spin_wait_until(target);
}

/**
 * @brief Busy-wait with occasional yields
 * 
 * Less aggressive than pure spin_wait but still low-latency.
 * Yields every yield_ns nanoseconds to avoid total CPU starvation.
 * 
 * @param duration_ns Total duration to wait
 * @param yield_ns How often to yield (e.g., every 100µs)
 */
inline void spin_wait_with_yields(uint64_t duration_ns, uint64_t yield_ns = 100000) noexcept {
    auto target = HighResolutionClock::now() + duration_ns;
    auto last_yield = HighResolutionClock::now();
    
    while (HighResolutionClock::now() < target) {
        auto now = HighResolutionClock::now();
        if (now - last_yield > yield_ns) {
            std::this_thread::yield();
            last_yield = now;
        }
    }
}

// ============================================================================
// Time Formatting Utilities
// ============================================================================

/**
 * @brief Format nanosecond duration as human-readable string
 * 
 * Examples:
 * - 500 -> "500ns"
 * - 50000 -> "50µs"
 * - 5000000 -> "5ms"
 * - 5000000000 -> "5s"
 */
inline std::string format_duration_ns(uint64_t ns) {
    std::ostringstream oss;
    
    if (ns < 1000) {
        oss << ns << "ns";
    } else if (ns < 1000000) {
        oss << std::fixed << std::setprecision(2) << (ns / 1000.0) << "µs";
    } else if (ns < 1000000000) {
        oss << std::fixed << std::setprecision(2) << (ns / 1000000.0) << "ms";
    } else {
        oss << std::fixed << std::setprecision(2) << (ns / 1000000000.0) << "s";
    }
    
    return oss.str();
}

/**
 * @brief Format microsecond duration as human-readable string
 */
inline std::string format_duration_us(uint64_t us) {
    return format_duration_ns(us * NS_PER_US);
}

/**
 * @brief Format millisecond duration as human-readable string
 */
inline std::string format_duration_ms(uint64_t ms) {
    return format_duration_ns(ms * NS_PER_MS);
}

} // namespace hft::core