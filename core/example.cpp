//example.cpp
#include "types.hpp"
#include "ring_buffer.hpp"
#include "logger.hpp"
#include "time.hpp"

#include <iostream>
#include <thread>
#include <vector>
#include <iomanip>
#include <algorithm>

using namespace hft::core;

// ============================================================================
// Example 1: Market Data Types
// ============================================================================

void example_market_data() {
    std::cout << "\n=== Example 1: Market Data Types ===\n";
    
    // Create a market tick
    Tick tick(
        HighResolutionClock::now(),  // timestamp
        100.50,                      // price
        1000000,                     // volume
        Side::BUY                    // side
    );
    
    std::cout << "Created tick: " << tick << std::endl;
    std::cout << "Tick size: " << sizeof(Tick) << " bytes (cache-line aligned)\n";
    
    // Create an order
    Order order(
        12345,              // order_id
        Side::SELL,         // side
        OrderType::LIMIT,   // type
        100.75,             // price
        5000                // quantity
    );
    
    order.creation_time = HighResolutionClock::now();
    order.last_update = order.creation_time;
    order.status = OrderStatus::ACCEPTED;
    order.filled = 2000;
    
    std::cout << "Created order: " << order << std::endl;
    std::cout << "Order fill: " << order.fill_percentage() << "%\n";
    std::cout << "Order size: " << sizeof(Order) << " bytes\n";
    
    // Create a trade
    Trade trade(
        HighResolutionClock::now(),
        999,                // trade_id
        order.order_id,     // buyer_order_id
        99999,              // seller_order_id
        100.75,             // trade_price
        2000                // trade_quantity
    );
    
    std::cout << "Created trade: " << trade << std::endl;
    std::cout << "Trade size: " << sizeof(Trade) << " bytes\n";
    
    // LOB snapshot
    LOBSnapshot lob;
    lob.timestamp = HighResolutionClock::now();
    lob.best_bid = 100.50;
    lob.best_ask = 100.51;
    lob.bid_volume = 50000;
    lob.ask_volume = 75000;
    
    std::cout << "LOB Snapshot - Mid: " << lob.mid_price() 
              << ", Spread: " << lob.spread() << std::endl;
}

// ============================================================================
// Example 2: Ring Buffer Communication
// ============================================================================

void example_ring_buffer() {
    std::cout << "\n=== Example 2: Lock-Free Ring Buffer ===\n";
    
    // Create a ring buffer for market ticks
    RingBuffer<Tick, 8192> tick_buffer;
    
    std::cout << "Ring buffer capacity: " << tick_buffer.capacity() << " ticks\n";
    
    // Producer thread - generate market data
    auto producer = [&tick_buffer]() {
        HFT_LOG_INFO("Producer thread started");
        
        for (int i = 0; i < 1000; ++i) {
            Tick tick(
                HighResolutionClock::now(),
                100.0 + (i % 100) * 0.01,  // Varying prices
                1000000 + (i % 500000),    // Varying volume
                i % 2 == 0 ? Side::BUY : Side::SELL
            );
            tick.sequence = i;
            
            if (!tick_buffer.enqueue(tick)) {
                HFT_LOGF_WARN("Buffer full at iteration %d, producer backing off", i);
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
            
            // Add some variation to producer rate
            if (i % 10 == 0) {
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            }
        }
        
        HFT_LOG_INFO("Producer thread finished");
    };
    
    // Consumer thread - process market data
    LatencyStats latency_stats;
    auto consumer = [&tick_buffer, &latency_stats]() {
        HFT_LOG_INFO("Consumer thread started");
        
        int processed = 0;
        int failed_pops = 0;
        
        while (processed < 1000) {
            Tick tick;
            
            {
                LatencyMeasurement measure(latency_stats);
                if (tick_buffer.dequeue(tick)) {
                    processed++;
                    
                    // Process the tick (simulated)
                    if (processed % 100 == 0) {
                        HFT_LOGF_DEBUG(
                            "Processed tick #%d: price=%.2f, volume=%lu",
                            processed, tick.price, tick.volume
                        );
                    }
                } else {
                    failed_pops++;
                    std::this_thread::yield();
                }
            }
            
            // Limit busy-waiting
            if (failed_pops > 100) {
                std::this_thread::sleep_for(std::chrono::microseconds(1));
                failed_pops = 0;
            }
        }
        
        HFT_LOG_INFO("Consumer thread finished");
    };
    
    // Run producer and consumer concurrently
    std::thread prod_thread(producer);
    std::thread cons_thread(consumer);
    
    prod_thread.join();
    cons_thread.join();
    
    // Print latency statistics
    std::cout << "Dequeue latency stats:\n"
              << "  Count: " << latency_stats.count() << " operations\n"
              << "  Min:   " << latency_stats.min_us() << " µs\n"
              << "  Max:   " << latency_stats.max_us() << " µs\n"
              << "  Avg:   " << latency_stats.avg_us() << " µs\n";
}

// ============================================================================
// Example 3: SPSCQueue with Explicit Producer/Consumer Roles
// ============================================================================

void example_spsc_queue() {
    std::cout << "\n=== Example 3: SPSC Queue with Role Separation ===\n";
    
    // Create an SPSC queue
    SPSCQueue<Order, 1024> order_queue;
    
    auto producer = [&order_queue]() {
        auto producer = order_queue.get_producer();
        
        HFT_LOG_INFO("Order producer started");
        
        for (int i = 0; i < 100; ++i) {
            Order order(
                100000 + i,
                i % 2 == 0 ? Side::BUY : Side::SELL,
                OrderType::LIMIT,
                100.0 + (i * 0.01),
                1000 + (i * 10)
            );
            order.creation_time = HighResolutionClock::now();
            
            while (!producer.push(order)) {
                HFT_LOGF_WARN("Order queue full, producer backing off");
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }
        
        HFT_LOG_INFO("Order producer finished");
    };
    
    auto consumer = [&order_queue]() {
        auto consumer = order_queue.get_consumer();
        
        HFT_LOG_INFO("Order consumer started");
        
        int processed = 0;
        while (processed < 100) {
            Order order;
            if (consumer.pop(order)) {
                processed++;
                
                if (processed % 20 == 0) {
                    HFT_LOGF_DEBUG(
                        "Processed order: id=%lu, side=%s, qty=%lu",
                        order.order_id,
                        order.side == Side::BUY ? "BUY" : "SELL",
                        order.quantity
                    );
                }
            } else {
                std::this_thread::yield();
            }
        }
        
        HFT_LOG_INFO("Order consumer finished");
    };
    
    std::thread prod(producer);
    std::thread cons(consumer);
    
    prod.join();
    cons.join();
}

// ============================================================================
// Example 4: Logging System
// ============================================================================

void example_logging() {
    std::cout << "\n=== Example 4: Logging System ===\n";
    
    // Set logging level
    get_logger().set_level(LogLevel::DEBUG);
    
    HFT_LOG_DEBUG("Debug message - detailed diagnostics");
    HFT_LOG_INFO("Info message - general information");
    HFT_LOG_WARN("Warn message - potential issues");
    HFT_LOG_ERROR("Error message - error conditions");
    
    // Formatted logging
    HFT_LOGF_INFO("Processed %d market updates", 1000000);
    HFT_LOGF_WARN("Queue utilization: %.2f%%", 85.5);
    HFT_LOGF_ERROR("Order rejected: error code %d", 42);
    
    // Scoped timer example
    {
        HFT_SCOPED_TIMER("Simulated work block", LogLevel::DEBUG);
        
        // Simulate some work
        volatile uint64_t sum = 0;
        for (uint64_t i = 0; i < 100000000; ++i) {
            sum += i;
        }
    }
    
    // Flush logs to disk
    get_logger().flush();
    
    std::cout << "Check 'hft.log' for logged output\n";
}

// ============================================================================
// Example 5: High-Resolution Time Utilities
// ============================================================================

void example_time_utilities() {
    std::cout << "\n=== Example 5: Time Utilities ===\n";
    
    // Get current time in different units
    Timestamp now_ns = HighResolutionClock::now();
    Timestamp now_us = HighResolutionClock::now_us();
    Timestamp now_ms = HighResolutionClock::now_ms();
    
    std::cout << "Current timestamps:\n"
              << "  Nanoseconds: " << now_ns << " ns\n"
              << "  Microseconds: " << now_us << " µs\n"
              << "  Milliseconds: " << now_ms << " ms\n";
    
    // Time conversions
    uint64_t duration_ns = 1234567;
    std::cout << "\nTime conversions of " << duration_ns << " ns:\n"
              << "  To µs: " << ns_to_us(duration_ns) << "\n"
              << "  To ms: " << ns_to_ms(duration_ns) << "\n"
              << "  To sec: " << std::fixed << std::setprecision(6) 
              << ns_to_sec(duration_ns) << "\n";
    
    // Latency measurement
    std::cout << "\nLatency measurement example:\n";
    
    LatencyStats stats;
    for (int i = 0; i < 10; ++i) {
        {
            LatencyMeasurement timer(stats);
            
            // Simulate variable latency work
            volatile uint64_t result = 0;
            for (uint64_t j = 0; j < (i + 1) * 1000000; ++j) {
                result ^= j;
            }
        }
    }
    
    std::cout << "  Min: " << format_duration_us(stats.min_us()) << "\n"
              << "  Max: " << format_duration_us(stats.max_us()) << "\n"
              << "  Avg: " << format_duration_us(stats.avg_us()) << "\n"
              << "  Count: " << stats.count() << "\n";
    
    // Format duration examples
    std::cout << "\nDuration formatting:\n"
              << "  500 ns: " << format_duration_ns(500) << "\n"
              << "  50000 ns: " << format_duration_ns(50000) << "\n"
              << "  5000000 ns: " << format_duration_ns(5000000) << "\n"
              << "  5000000000 ns: " << format_duration_ns(5000000000) << "\n";
}

// ============================================================================
// Example 6: Complete Trading System Simulation
// ============================================================================

struct OrderBook {
    std::vector<LOBLevel> bids;
    std::vector<LOBLevel> asks;
    LOBSnapshot snapshot;
    
    void update_snapshot() {
        if (!bids.empty() && !asks.empty()) {
            snapshot.timestamp = HighResolutionClock::now();
            snapshot.best_bid = bids[0].price;
            snapshot.best_ask = asks[0].price;
            snapshot.bid_volume = bids[0].volume;
            snapshot.ask_volume = asks[0].volume;
            snapshot.bid_count = bids[0].order_count;
            snapshot.ask_count = asks[0].order_count;
        }
    }
};

void example_trading_simulation() {
    std::cout << "\n=== Example 6: Trading System Simulation ===\n";
    
    HFT_LOG_INFO("Starting trading system simulation");
    
    // Shared data structures
    RingBuffer<Tick, 16384> market_data_queue;
    RingBuffer<Order, 4096> order_queue;
    OrderBook book;
    
    LatencyStats market_latency;
    LatencyStats order_latency;
    
    // Market data simulator
    auto market_simulator = [&market_data_queue]() {
        std::cout << "[Market] Simulator started\n";
        
        Price price = 100.00;
        
        for (int i = 0; i < 500; ++i) {
            // Generate synthetic market data with realistic movements
            price += (rand() % 100 - 50) * 0.001;  // ±5 cents per tick
            
            // Bid side
            Tick bid(
                HighResolutionClock::now(),
                price - 0.01,
                500000 + rand() % 100000,
                Side::BUY
            );
            bid.sequence = i * 2;
            market_data_queue.enqueue(bid);
            
            // Ask side
            Tick ask(
                HighResolutionClock::now(),
                price + 0.01,
                400000 + rand() % 100000,
                Side::SELL
            );
            ask.sequence = i * 2 + 1;
            market_data_queue.enqueue(ask);
            
            // Small delay to simulate realistic tick rate
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        
        std::cout << "[Market] Simulator finished\n";
    };
    
    // Trading engine
    auto trading_engine = [&market_data_queue, &order_queue, &book, 
                          &market_latency, &order_latency]() {
        std::cout << "[Engine] Started\n";
        
        int ticks_processed = 0;
        int orders_created = 0;
        
        while (ticks_processed < 1000) {
            Tick tick;
            
            {
                LatencyMeasurement timer(market_latency);
                if (market_data_queue.dequeue(tick)) {
                    ticks_processed++;
                    
                    // Update order book
                    if (tick.side == Side::BUY) {
                        // Update bid
                        if (book.bids.empty() || book.bids[0].price != tick.price) {
                            book.bids.clear();
                            book.bids.emplace_back(tick.price, tick.volume);
                        } else {
                            book.bids[0].volume = tick.volume;
                        }
                    } else {
                        // Update ask
                        if (book.asks.empty() || book.asks[0].price != tick.price) {
                            book.asks.clear();
                            book.asks.emplace_back(tick.price, tick.volume);
                        } else {
                            book.asks[0].volume = tick.volume;
                        }
                    }
                    
                    // Update snapshot
                    book.update_snapshot();
                    
                    // Generate trading signals (simple momentum strategy)
                    if (ticks_processed % 50 == 0 && orders_created < 100) {
                        Order order(
                            200000 + orders_created,
                            orders_created % 2 == 0 ? Side::BUY : Side::SELL,
                            OrderType::LIMIT,
                            book.snapshot.best_bid + 0.005,
                            100 + (orders_created % 900)
                        );
                        order.creation_time = HighResolutionClock::now();
                        
                        {
                            LatencyMeasurement order_timer(order_latency);
                            if (order_queue.enqueue(order)) {
                                orders_created++;
                            }
                        }
                    }
                    
                    if (ticks_processed % 100 == 0) {
                        HFT_LOGF_DEBUG(
                            "Processed %d ticks, created %d orders, "
                            "mid=%.2f, spread=%.4f",
                            ticks_processed, orders_created,
                            book.snapshot.mid_price(),
                            book.snapshot.spread()
                        );
                    }
                } else {
                    std::this_thread::yield();
                }
            }
        }
        
        std::cout << "[Engine] Finished - processed " << ticks_processed 
                  << " ticks, created " << orders_created << " orders\n";
    };
    
    // Execute simulation
    std::thread market_thread(market_simulator);
    std::thread engine_thread(trading_engine);
    
    market_thread.join();
    engine_thread.join();
    
    // Print performance metrics
    std::cout << "\nPerformance Metrics:\n"
              << "Market data processing:\n"
              << "  Latency (avg): " << format_duration_us(market_latency.avg_us()) << "\n"
              << "  Latency (max): " << format_duration_us(market_latency.max_us()) << "\n"
              << "Order queue access:\n"
              << "  Latency (avg): " << format_duration_us(order_latency.avg_us()) << "\n"
              << "  Latency (max): " << format_duration_us(order_latency.max_us()) << "\n";
    
    HFT_LOG_INFO("Trading system simulation completed");
    get_logger().flush();
}

// ============================================================================
// Main Entry Point
// ============================================================================

int main() {
    std::cout << "===============================================\n"
              << "  HFT Core Module Examples\n"
              << "===============================================\n";
    
    // Initialize logger
    init_logger("hft_example.log", LogLevel::DEBUG);
    HFT_LOG_INFO("HFT Core Module Examples Starting");
    
    try {
        example_market_data();
        example_ring_buffer();
        example_spsc_queue();
        example_logging();
        example_time_utilities();
        example_trading_simulation();
        
        std::cout << "\n===============================================\n"
                  << "  All examples completed successfully!\n"
                  << "===============================================\n";
        
        HFT_LOG_INFO("All examples completed successfully");
        get_logger().flush();
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        HFT_LOGF_ERROR("Fatal error: %s", e.what());
        return 1;
    }
}