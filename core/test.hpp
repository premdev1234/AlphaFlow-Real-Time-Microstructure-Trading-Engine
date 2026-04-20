// test.hpp 
#pragma once

#include "types.hpp"
#include "ring_buffer.hpp"
#include "logger.hpp"
#include "time.hpp"

#include <iostream>
#include <cassert>
#include <thread>
#include <vector>
#include <algorithm>
#include <iomanip>
#include <functional>
#include <cmath>
namespace hft::core::test {

// ============================================================================
// Test Utilities
// ============================================================================

class TestRunner {
public:
    TestRunner() : passed_(0), failed_(0) {}
    
    void run_test(const std::string& name, std::function<bool()> test) {
        try {
            if (test()) {
                std::cout << "[PASS] " << name << std::endl;
                passed_++;
            } else {
                std::cout << "[FAIL] " << name << std::endl;
                failed_++;
            }
        } catch (const std::exception& e) {
            std::cout << "[ERROR] " << name << ": " << e.what() << std::endl;
            failed_++;
        }
    }
    
    void print_summary() {
        std::cout << "\n========================================\n"
                  << "Test Results: " << passed_ << " passed, " 
                  << failed_ << " failed\n"
                  << "========================================\n";
    }
    
    bool all_passed() const { return failed_ == 0; }
    
private:
    int passed_;
    int failed_;
};

// ============================================================================
// Type System Tests
// ============================================================================

bool test_tick_alignment() {
    assert(sizeof(Tick) == 64);
    assert(alignof(Tick) == 64);
    return true;
}

bool test_order_alignment() {
    assert(sizeof(Order) == 64);
    assert(alignof(Order) == 64);
    return true;
}

bool test_trade_alignment() {
    assert(sizeof(Trade) == 64);
    assert(alignof(Trade) == 64);
    return true;
}

bool test_lob_snapshot_alignment() {
    assert(sizeof(LOBSnapshot) == 64);
    assert(alignof(LOBSnapshot) == 64);
    return true;
}

bool test_tick_creation() {
    Tick tick(12345, 100.50, 1000000, Side::BUY);
    assert(tick.timestamp == 12345);
    assert(tick.price == 100.50);
    assert(tick.volume == 1000000);
    assert(tick.side == Side::BUY);
    return true;
}

bool test_order_fill_percentage() {
    Order order(100, Side::BUY, OrderType::LIMIT, 100.0, 1000);
    order.filled = 250;
    assert(order.fill_percentage() == 25.0);
    return true;
}

bool test_order_remaining() {
    Order order(100, Side::BUY, OrderType::LIMIT, 100.0, 1000);
    order.filled = 600;
    assert(order.remaining() == 400);
    return true;
}

bool test_lob_snapshot_mid_price() {
    LOBSnapshot lob;
    lob.best_bid = 100.00;
    lob.best_ask = 100.02;
    assert(std::abs(lob.mid_price() - 100.01) < 1e-9);
    return true;
}

bool test_lob_snapshot_spread() {
    LOBSnapshot lob;
    lob.best_bid = 100.00;
    lob.best_ask = 100.02;
    
    assert(std::abs(lob.spread() - 0.02) < 1e-9);
    return true;
}

// ============================================================================
// Ring Buffer Tests
// ============================================================================

bool test_ring_buffer_empty() {
    RingBuffer<Tick, 256> buffer;
    assert(buffer.empty());
    assert(buffer.size() == 0);
    return true;
}

bool test_ring_buffer_enqueue_dequeue() {
    RingBuffer<Tick, 256> buffer;
    
    Tick original(12345, 100.50, 1000, Side::BUY);
    assert(buffer.enqueue(original));
    
    Tick dequeued;
    assert(buffer.dequeue(dequeued));
    
    assert(dequeued.timestamp == original.timestamp);
    assert(dequeued.price == original.price);
    assert(dequeued.volume == original.volume);
    
    return true;
}

bool test_ring_buffer_full() {
    RingBuffer<Tick, 4> buffer;
    
    Tick tick(0, 0, 0, Side::BUY);
    
    // Fill buffer (max SIZE - 1 elements for SPSC)
    assert(buffer.enqueue(tick));
    assert(buffer.enqueue(tick));
    assert(buffer.enqueue(tick));
    
    // Should be full now
    assert(buffer.full());
    assert(!buffer.enqueue(tick));
    
    return true;
}

bool test_ring_buffer_wraparound() {
    RingBuffer<uint32_t, 8> buffer;
    
    // Fill and empty multiple times to test wraparound
    for (int cycle = 0; cycle < 10; ++cycle) {
        for (uint32_t i = 0; i < 4; ++i) {
            assert(buffer.enqueue(i + cycle * 100));
        }
        
        for (uint32_t i = 0; i < 4; ++i) {
            uint32_t val;
            assert(buffer.dequeue(val));
            assert(val == i + cycle * 100);
        }
    }
    
    assert(buffer.empty());
    return true;
}

bool test_ring_buffer_batch_operations() {
    RingBuffer<int, 256> buffer;
    
    int values[] = {1, 2, 3, 4, 5};
    int count = sizeof(values) / sizeof(values[0]);
    
    // Enqueue batch
    assert(buffer.enqueue_batch(values, count) == count);
    
    // Dequeue batch
    int out[5];
    assert(buffer.dequeue_batch(out, count) == count);
    
    for (int i = 0; i < count; ++i) {
        assert(out[i] == values[i]);
    }
    
    return true;
}

bool test_ring_buffer_concurrent() {
    RingBuffer<Tick, 1024> buffer;
    
    int count = 1000;
    int produced = 0;
    int consumed = 0;
    
    auto producer = [&]() {
        for (int i = 0; i < count; ++i) {
            Tick tick(i, 100.0 + i * 0.01, 1000 + i, Side::BUY);
            while (!buffer.enqueue(tick)) {
                std::this_thread::yield();
            }
            produced++;
        }
    };
    
    auto consumer = [&]() {
        while (consumed < count) {
            Tick tick;
            if (buffer.dequeue(tick)) {
                consumed++;
            } else {
                std::this_thread::yield();
            }
        }
    };
    
    std::thread p(producer);
    std::thread c(consumer);
    
    p.join();
    c.join();
    
    assert(produced == count);
    assert(consumed == count);
    assert(buffer.empty());
    
    return true;
}

// ============================================================================
// Logger Tests
// ============================================================================

bool test_logger_creation() {
    // Logger should exist and be functional
    auto& logger = get_logger();
    logger.set_level(LogLevel::DEBUG);
    return true;
}

bool test_logger_log() {
    get_logger().set_level(LogLevel::TRACE);
    get_logger().log(LogLevel::INFO, "Test message");
    return true;
}

bool test_logger_format() {
    get_logger().logf(LogLevel::INFO, "Number: %d, Float: %.2f", 42, 3.14);
    return true;
}

bool test_logger_levels() {
    auto& logger = get_logger();
    
    logger.set_level(LogLevel::WARN);
    
    // These should be logged
    logger.log(LogLevel::WARN, "Warning");
    logger.log(LogLevel::ERROR, "Error");
    
    // These should not
    logger.log(LogLevel::DEBUG, "Debug (filtered)");
    logger.log(LogLevel::TRACE, "Trace (filtered)");
    
    return true;
}

bool test_logger_buffer_utilization() {
    auto& logger = get_logger();
    double util = logger.buffer_utilization();
    return util >= 0.0 && util <= 1.0;
}

bool test_scoped_timer() {
    LatencyStats stats;
    {
        ScopedTimer timer("Test", LogLevel::TRACE);
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    
    // Just verify it doesn't crash
    return true;
}

// ============================================================================
// Time Utility Tests
// ============================================================================

bool test_clock_monotonic() {
    Timestamp t1 = HighResolutionClock::now();
    std::this_thread::sleep_for(std::chrono::microseconds(10));
    Timestamp t2 = HighResolutionClock::now();
    
    assert(t2 > t1);
    return true;
}

bool test_time_conversions() {
    uint64_t ns = 1000000000;  // 1 second in ns
    
    assert(ns_to_us(ns) == 1000000);
    assert(ns_to_ms(ns) == 1000);
    assert(ns_to_sec(ns) == 1.0);
    
    assert(us_to_ns(1000000) == ns);
    assert(ms_to_ns(1000) == ns);
    
    return true;
}

bool test_latency_stats_recording() {
    LatencyStats stats;
    
    stats.record_ns(1000);
    stats.record_ns(2000);
    stats.record_ns(3000);
    
    assert(stats.min_ns() == 1000);
    assert(stats.max_ns() == 3000);
    assert(stats.avg_ns() == 2000);
    assert(stats.count() == 3);
    
    return true;
}

bool test_latency_measurement() {
    LatencyStats stats;
    
    {
        LatencyMeasurement timer(stats);
        std::this_thread::sleep_for(std::chrono::microseconds(10));
    }
    
    assert(stats.count() == 1);
    assert(stats.min_ns() > 0);
    assert(stats.max_ns() > 0);
    
    return true;
}

bool test_duration_formatting() {
    std::string s1 = format_duration_ns(500);
    std::string s2 = format_duration_ns(50000);
    std::string s3 = format_duration_ns(5000000);
    
    assert(!s1.empty());
    assert(!s2.empty());
    assert(!s3.empty());
    
    return true;
}

// ============================================================================
// Performance Benchmarks
// ============================================================================

void benchmark_ring_buffer_throughput() {
    std::cout << "\n=== Ring Buffer Throughput Benchmark ===\n";
    
    const int ITERATIONS = 10000000;
    RingBuffer<uint64_t, 8192> buffer;
    
    LatencyStats enqueue_latency;
    LatencyStats dequeue_latency;
    
    auto producer = [&]() {
        for (int i = 0; i < ITERATIONS; ++i) {
            {
                LatencyMeasurement timer(enqueue_latency);
                while (!buffer.enqueue(i)) {}
            }
        }
    };
    
    auto consumer = [&]() {
        for (int i = 0; i < ITERATIONS; ++i) {
            uint64_t val;
            {
                LatencyMeasurement timer(dequeue_latency);
                while (!buffer.dequeue(val)) {}
            }
        }
    };
    
    auto start = HighResolutionClock::now();
    
    std::thread p(producer);
    std::thread c(consumer);
    
    p.join();
    c.join();
    
    auto end = HighResolutionClock::now();
    auto total_ns = end - start;
    
    std::cout << "Operations: " << ITERATIONS << "\n"
              << "Total Time: " << format_duration_ns(total_ns) << "\n"
              << "Throughput: " << (ITERATIONS * 2 * 1e9 / total_ns) << " ops/sec\n"
              << "Enqueue Latency (avg): " << format_duration_ns(enqueue_latency.avg_ns()) << "\n"
              << "Dequeue Latency (avg): " << format_duration_ns(dequeue_latency.avg_ns()) << "\n";
}

void benchmark_time_resolution() {
    std::cout << "\n=== Time Resolution Benchmark ===\n";
    
    const int SAMPLES = 100000;
    LatencyStats stats;
    
    for (int i = 0; i < SAMPLES; ++i) {
        auto start = HighResolutionClock::now();
        auto end = HighResolutionClock::now();
        auto diff = end - start;
        if (diff > 0) stats.record_ns(diff);
    }
    
    std::cout << "Samples: " << stats.count() << "\n"
              << "Min Timer Resolution: " << format_duration_ns(stats.min_ns()) << "\n"
              << "Max Timer Jitter: " << format_duration_ns(stats.max_ns()) << "\n"
              << "Avg Overhead: " << format_duration_ns(stats.avg_ns()) << "\n";
}

void benchmark_logging() {
    std::cout << "\n=== Logging Performance Benchmark ===\n";
    
    get_logger().set_level(LogLevel::DEBUG);
    
    const int ITERATIONS = 100000;
    LatencyStats stats;
    
    auto start = HighResolutionClock::now();
    
    for (int i = 0; i < ITERATIONS; ++i) {
        {
            LatencyMeasurement timer(stats);
            get_logger().logf(LogLevel::DEBUG, "Message %d", i);
        }
    }
    
    auto end = HighResolutionClock::now();
    
    std::cout << "Log Operations: " << ITERATIONS << "\n"
              << "Total Time: " << format_duration_ns(end - start) << "\n"
              << "Avg Latency: " << format_duration_ns(stats.avg_ns()) << "\n"
              << "Max Latency: " << format_duration_ns(stats.max_ns()) << "\n";
    
    get_logger().flush();
}

// ============================================================================
// Run All Tests
// ============================================================================

void run_all_tests() {
    TestRunner runner;
    
    std::cout << "=== HFT Core Module Tests ===\n\n";
    
    std::cout << "--- Type System Tests ---\n";
    runner.run_test("Tick Alignment", test_tick_alignment);
    runner.run_test("Order Alignment", test_order_alignment);
    runner.run_test("Trade Alignment", test_trade_alignment);
    runner.run_test("LOB Snapshot Alignment", test_lob_snapshot_alignment);
    runner.run_test("Tick Creation", test_tick_creation);
    runner.run_test("Order Fill Percentage", test_order_fill_percentage);
    runner.run_test("Order Remaining", test_order_remaining);
    runner.run_test("LOB Mid Price", test_lob_snapshot_mid_price);
    runner.run_test("LOB Spread", test_lob_snapshot_spread);
    
    std::cout << "\n--- Ring Buffer Tests ---\n";
    runner.run_test("Empty Buffer", test_ring_buffer_empty);
    runner.run_test("Enqueue/Dequeue", test_ring_buffer_enqueue_dequeue);
    runner.run_test("Full Buffer", test_ring_buffer_full);
    runner.run_test("Wraparound", test_ring_buffer_wraparound);
    runner.run_test("Batch Operations", test_ring_buffer_batch_operations);
    runner.run_test("Concurrent Access", test_ring_buffer_concurrent);
    
    std::cout << "\n--- Logger Tests ---\n";
    runner.run_test("Logger Creation", test_logger_creation);
    runner.run_test("Simple Log", test_logger_log);
    runner.run_test("Formatted Log", test_logger_format);
    runner.run_test("Log Levels", test_logger_levels);
    runner.run_test("Buffer Utilization", test_logger_buffer_utilization);
    runner.run_test("Scoped Timer", test_scoped_timer);
    
    std::cout << "\n--- Time Utility Tests ---\n";
    runner.run_test("Clock Monotonic", test_clock_monotonic);
    runner.run_test("Time Conversions", test_time_conversions);
    runner.run_test("Latency Stats", test_latency_stats_recording);
    runner.run_test("Latency Measurement", test_latency_measurement);
    runner.run_test("Duration Formatting", test_duration_formatting);
    
    runner.print_summary();
    
    if (runner.all_passed()) {
        std::cout << "\nRunning Performance Benchmarks...\n";
        benchmark_ring_buffer_throughput();
        benchmark_time_resolution();
        benchmark_logging();
    }
}

} // namespace hft::core::test

// ============================================================================
// Main Entry Point for Test Runner
// ============================================================================

/*
int main() {
    hft::core::init_logger("test.log", hft::core::LogLevel::DEBUG);
    hft::core::test::run_all_tests();
    hft::core::get_logger().flush();
    return 0;
}
*/