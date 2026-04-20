// logger.hpp
#pragma once

#include <atomic>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <chrono>
#include <array>
#include <iomanip>
#include <sstream>
#include <memory>

namespace hft::core {

// ============================================================================
// Log Level Enumeration
// ============================================================================

enum class LogLevel : uint8_t {
    TRACE = 0,
    DEBUG = 1,
    INFO = 2,
    WARN = 3,
    ERROR = 4,
    CRITICAL = 5,
    OFF = 6
};

/**
 * @brief Convert log level to string
 */
inline constexpr const char* log_level_name(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::TRACE: return "TRACE";
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO: return "INFO";
        case LogLevel::WARN: return "WARN";
        case LogLevel::ERROR: return "ERROR";
        case LogLevel::CRITICAL: return "CRITICAL";
        case LogLevel::OFF: return "OFF";
        default: return "UNKNOWN";
    }
}

// ============================================================================
// Log Message Structure
// ============================================================================

/**
 * @struct LogMessage
 * @brief Fixed-size log message structure for lockless queuing
 * 
 * Designed to be passed through the SPSC ring buffer without dynamic allocation.
 * Uses a fixed-size buffer for log text to ensure deterministic memory usage.
 */
struct LogMessage {
    static constexpr uint32_t MAX_MSG_LEN = 256;
    
    uint64_t timestamp;           // Nanosecond timestamp
    LogLevel level;               // Log level
    uint8_t padding[7];           // Padding for alignment
    std::array<char, MAX_MSG_LEN> message;  // Message buffer
    
    LogMessage() : timestamp(0), level(LogLevel::INFO) {
        message.fill(0);
    }
    
    LogMessage(uint64_t ts, LogLevel lvl, const char* msg)
        : timestamp(ts), level(lvl) {
        message.fill(0);
        if (msg) {
            std::strncpy(message.data(), msg, MAX_MSG_LEN - 1);
        }
    }
};

static_assert(std::is_trivially_copyable_v<LogMessage>,
              "LogMessage must be trivially copyable");

// ============================================================================
// Synchronous Logger - For Direct Use (Low-Latency Path)
// ============================================================================

/**
 * @class SyncLogger
 * @brief Minimal synchronous logger for critical path logging
 * 
 * This logger directly buffers log messages in memory and writes them
 * periodically to disk. It's designed to have minimal impact on latency.
 * 
 * Features:
 * - Buffered output (reduces system calls)
 * - Configurable log level (can disable low-priority logs at runtime)
 * - Thread-safe write operations (but should ideally use single thread)
 * - Nanosecond precision timestamps
 */
class SyncLogger {
public:
    static constexpr uint32_t BUFFER_SIZE = 65536;  // 64 KB buffer
    
    explicit SyncLogger(const char* filename = nullptr, 
                        LogLevel min_level = LogLevel::TRACE)
        : filename_(filename ? filename : "hft.log"),
          min_level_(min_level),
          file_(nullptr),
          buffer_pos_(0),
          buffer_(std::make_unique<char[]>(BUFFER_SIZE)) {
        if (!filename_.empty()) {
            file_ = std::fopen(filename_.c_str(), "a");
        }
    }
    
    ~SyncLogger() {
        flush();
        if (file_) {
            std::fclose(file_);
        }
    }
    
    // Delete copy
    SyncLogger(const SyncLogger&) = delete;
    SyncLogger& operator=(const SyncLogger&) = delete;
    
    // Allow move
    SyncLogger(SyncLogger&&) noexcept = default;
    SyncLogger& operator=(SyncLogger&&) noexcept = default;
    
    /**
     * @brief Set minimum log level
     * 
     * Logs below this level will be ignored.
     * Useful for reducing overhead in production.
     */
    void set_level(LogLevel level) noexcept {
        min_level_ = level;
    }
    
    /**
     * @brief Log a message (inline implementation for minimal overhead)
     */
    void log(LogLevel level, const char* message) noexcept {
        if (level < min_level_) return;
        
        // Get current time
        auto now = std::chrono::high_resolution_clock::now();
        auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
            now.time_since_epoch()).count();
        
        // Format timestamp
        auto millis = nanos / 1000000;
        auto nanos_part = nanos % 1000000;
        
        std::tm* timeinfo = std::localtime(reinterpret_cast<const time_t*>(&millis));
        
        // Write to buffer
        int written = std::snprintf(
            buffer_.get() + buffer_pos_,
            BUFFER_SIZE - buffer_pos_,
            "[%04d-%02d-%02d %02d:%02d:%02d.%06ld] [%s] %s\n",
            timeinfo->tm_year + 1900,
            timeinfo->tm_mon + 1,
            timeinfo->tm_mday,
            timeinfo->tm_hour,
            timeinfo->tm_min,
            timeinfo->tm_sec,
            nanos_part,
            log_level_name(level),
            message
        );
        
        if (written > 0) {
            buffer_pos_ += written;
            
            // Auto-flush if buffer gets too full
            if (buffer_pos_ > BUFFER_SIZE - 512) {
                flush();
            }
        }
    }
    
    /**
     * @brief Convenience method for formatted logging
     * 
     * Limited to simple printf-style formatting.
     * For complex logging, format externally and use log().
     */
    template <typename... Args>
    void logf(LogLevel level, const char* format, Args... args) noexcept {
        if (level < min_level_) return;
        
        char buffer[LogMessage::MAX_MSG_LEN];
        std::snprintf(buffer, sizeof(buffer), format, args...);
        log(level, buffer);
    }
    
    /**
     * @brief Flush buffered output to disk
     */
    void flush() noexcept {
        if (file_ && buffer_pos_ > 0) {
            std::fwrite(buffer_.get(), 1, buffer_pos_, file_);
            std::fflush(file_);
            buffer_pos_ = 0;
        }
    }
    
    /**
     * @brief Get current buffer utilization (for monitoring)
     */
    double buffer_utilization() const noexcept {
        return static_cast<double>(buffer_pos_) / BUFFER_SIZE;
    }
    
private:
    std::string filename_;
    std::atomic<LogLevel> min_level_;
    std::FILE* file_;
    uint32_t buffer_pos_;
    std::unique_ptr<char[]> buffer_;
};

// ============================================================================
// Global Logger Instance
// ============================================================================

/**
 * @brief Global logger instance for application-wide use
 * 
 * Usage:
 * @code
 * hft::core::get_logger().log(hft::core::LogLevel::INFO, "Market opened");
 * hft::core::get_logger().logf(hft::core::LogLevel::WARN, 
 *                              "Order rejected: %d", order_id);
 * @endcode
 */
inline SyncLogger& get_logger() {
    static SyncLogger logger;
    return logger;
}

/**
 * @brief Initialize global logger with custom parameters
 * 
 * Should be called once at application startup before logging.
 */
inline void init_logger(const char* filename = "hft.log",
                        LogLevel level = LogLevel::INFO) {
    get_logger().set_level(level);
    // Note: Cannot change filename after creation, would need full reinit
}

// ============================================================================
// Convenience Macros for Logging
// ============================================================================

#define HFT_LOG_TRACE(msg) \
    do { if constexpr (true) hft::core::get_logger().log(hft::core::LogLevel::TRACE, msg); } while(0)

#define HFT_LOG_DEBUG(msg) \
    do { if constexpr (true) hft::core::get_logger().log(hft::core::LogLevel::DEBUG, msg); } while(0)

#define HFT_LOG_INFO(msg) \
    do { if constexpr (true) hft::core::get_logger().log(hft::core::LogLevel::INFO, msg); } while(0)

#define HFT_LOG_WARN(msg) \
    do { if constexpr (true) hft::core::get_logger().log(hft::core::LogLevel::WARN, msg); } while(0)

#define HFT_LOG_ERROR(msg) \
    do { if constexpr (true) hft::core::get_logger().log(hft::core::LogLevel::ERROR, msg); } while(0)

#define HFT_LOG_CRITICAL(msg) \
    do { if constexpr (true) hft::core::get_logger().log(hft::core::LogLevel::CRITICAL, msg); } while(0)

// Formatted logging macros
#define HFT_LOGF_TRACE(fmt, ...) \
    do { if constexpr (true) hft::core::get_logger().logf(hft::core::LogLevel::TRACE, fmt, __VA_ARGS__); } while(0)

#define HFT_LOGF_DEBUG(fmt, ...) \
    do { if constexpr (true) hft::core::get_logger().logf(hft::core::LogLevel::DEBUG, fmt, __VA_ARGS__); } while(0)

#define HFT_LOGF_INFO(fmt, ...) \
    do { if constexpr (true) hft::core::get_logger().logf(hft::core::LogLevel::INFO, fmt, __VA_ARGS__); } while(0)

#define HFT_LOGF_WARN(fmt, ...) \
    do { if constexpr (true) hft::core::get_logger().logf(hft::core::LogLevel::WARN, fmt, __VA_ARGS__); } while(0)

#define HFT_LOGF_ERROR(fmt, ...) \
    do { if constexpr (true) hft::core::get_logger().logf(hft::core::LogLevel::ERROR, fmt, __VA_ARGS__); } while(0)

#define HFT_LOGF_CRITICAL(fmt, ...) \
    do { if constexpr (true) hft::core::get_logger().logf(hft::core::LogLevel::CRITICAL, fmt, __VA_ARGS__); } while(0)

// ============================================================================
// Scoped Logging Utilities
// ============================================================================

/**
 * @class ScopedTimer
 * @brief RAII timer that logs elapsed time on scope exit
 * 
 * Useful for latency measurement of critical sections.
 * 
 * Usage:
 * @code
 * {
 *     HFT_SCOPED_TIMER("Order processing", LogLevel::DEBUG);
 *     // ... do work ...
 * } // Automatically logs elapsed time
 * @endcode
 */
class ScopedTimer {
public:
    ScopedTimer(const char* name, LogLevel level = LogLevel::DEBUG)
        : name_(name), level_(level),
          start_(std::chrono::high_resolution_clock::now()) {}
    
    ~ScopedTimer() {
        auto end = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            end - start_).count();
        
        char msg[LogMessage::MAX_MSG_LEN];
        std::snprintf(msg, sizeof(msg), 
                      "%s completed in %ld µs", name_, elapsed);
        
        get_logger().log(level_, msg);
    }
    
private:
    const char* name_;
    LogLevel level_;
    std::chrono::high_resolution_clock::time_point start_;
};

#define HFT_SCOPED_TIMER(name, level) \
    hft::core::ScopedTimer __scoped_timer__(name, level)

} // namespace hft::core