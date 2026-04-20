// ring_buffer.hpp
#pragma once

#include <atomic>
#include <cstring>
#include <cstdint>
#include <utility>
#include <memory>
#include <cassert>

namespace hft::core {

/**
 * @class RingBuffer
 * @brief High-performance lock-free single-producer single-consumer ring buffer
 * 
 * This is a wait-free circular buffer designed for ultra-low latency inter-thread
 * communication. Key characteristics:
 * 
 * - Lock-free: No mutexes, only atomic operations
 * - Single Producer, Single Consumer: Optimized for one thread producing, one consuming
 * - Memory Efficient: Circular buffer with minimal overhead
 * - Cache Friendly: Head and tail on separate cache lines to avoid false sharing
 * - Deterministic: No allocations after initialization
 * 
 * Template Parameters:
 * @tparam T      Element type (should be trivially copyable for performance)
 * @tparam SIZE   Capacity (must be power of 2 for efficient modulo)
 * 
 * Memory Layout:
 * - Producer head and consumer tail are on separate cache lines
 * - Actual data array follows
 * - This prevents false sharing and cache coherency traffic
 * 
 * Usage Example:
 * @code
 * RingBuffer<Tick, 8192> buffer;  // 8192 slots for Tick objects
 * 
 * // Producer thread
 * Tick tick = create_tick();
 * while (!buffer.enqueue(tick)) {
 *     // Buffer full - apply backpressure or drop
 * }
 * 
 * // Consumer thread
 * Tick tick;
 * if (buffer.dequeue(tick)) {
 *     process(tick);
 * }
 * @endcode
 */
template <typename T, uint32_t SIZE>
class RingBuffer {
private:
    // Verify SIZE is power of 2 for efficient masking
    static_assert((SIZE & (SIZE - 1)) == 0, "SIZE must be power of 2");
    static_assert(SIZE > 0, "SIZE must be positive");
    
    // Mask for efficient modulo operation (SIZE - 1)
    static constexpr uint32_t MASK = SIZE - 1;
    
    // Cache line size (typical 64 bytes on modern x86)
    static constexpr uint32_t CACHE_LINE = 64;
    
public:
    explicit RingBuffer() 
        : data_(std::make_unique<T[]>(SIZE)),
          producer_head_(0),
          consumer_tail_(0) {
        // Verify T is suitable for this use (should be trivially copyable)
        static_assert(std::is_trivially_copyable_v<T>,
                      "RingBuffer element type must be trivially copyable");
    }
    
    // Delete copy operations - each buffer should be owned by a single pair
    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;
    
    // Move operations are allowed
    RingBuffer(RingBuffer&&) noexcept = default;
    RingBuffer& operator=(RingBuffer&&) noexcept = default;
    
    ~RingBuffer() = default;
    
    /**
     * @brief Enqueue an element (producer thread only)
     * 
     * @param value Element to enqueue
     * @return true if successfully enqueued, false if buffer is full
     * 
     * This should only be called from the producer thread.
     * Uses std::memory_order_release to ensure the data is visible to consumer
     * before the head pointer is updated.
     */
    bool enqueue(const T& value) noexcept {
        // Load current head with acquire semantics (relaxed is OK here as producer only)
        const uint32_t head = producer_head_.load(std::memory_order_relaxed);
        
        // Calculate next head position
        const uint32_t next_head = (head + 1) & MASK;
        
        // Load consumer tail with acquire semantics to check for wrap-around
        const uint32_t tail = consumer_tail_.load(std::memory_order_acquire);
        
        // Check if buffer is full
        if (next_head == tail) {
            return false;  // Buffer full, cannot enqueue
        }
        
        // Write element to buffer
        data_[head] = value;
        
        // Update head pointer with release semantics
        // This ensures the write to data_[head] is visible before head is updated
        producer_head_.store(next_head, std::memory_order_release);
        
        return true;
    }
    
    /**
     * @brief Enqueue multiple elements (producer thread only)
     * 
     * Attempts to enqueue all elements. Returns the number successfully enqueued.
     * 
     * @param values Array of elements
     * @param count Number of elements to enqueue
     * @return Number of elements successfully enqueued
     */
    uint32_t enqueue_batch(const T* values, uint32_t count) noexcept {
        if (!values || count == 0) return 0;
        
        uint32_t enqueued = 0;
        for (uint32_t i = 0; i < count; ++i) {
            if (enqueue(values[i])) {
                enqueued++;
            } else {
                break;  // Stop at first failure
            }
        }
        return enqueued;
    }
    
    /**
     * @brief Dequeue an element (consumer thread only)
     * 
     * @param[out] value Element dequeued (valid only if return is true)
     * @return true if an element was dequeued, false if buffer is empty
     * 
     * This should only be called from the consumer thread.
     * Uses std::memory_order_release to ensure tail pointer updates are visible
     * to the producer before new space can be allocated.
     */
    bool dequeue(T& value) noexcept {
        // Load current tail with relaxed semantics (consumer only reads tail)
        const uint32_t tail = consumer_tail_.load(std::memory_order_relaxed);
        
        // Load producer head with acquire semantics to check for empty
        const uint32_t head = producer_head_.load(std::memory_order_acquire);
        
        // Check if buffer is empty
        if (tail == head) {
            return false;  // Buffer empty
        }
        
        // Read element from buffer
        value = data_[tail];
        
        // Update tail pointer with release semantics
        // This ensures the read from data_[tail] is complete before tail is updated
        consumer_tail_.store((tail + 1) & MASK, std::memory_order_release);
        
        return true;
    }
    
    /**
     * @brief Dequeue multiple elements (consumer thread only)
     * 
     * Attempts to dequeue up to count elements.
     * 
     * @param[out] values Array to store dequeued elements
     * @param count Maximum number of elements to dequeue
     * @return Number of elements successfully dequeued
     */
    uint32_t dequeue_batch(T* values, uint32_t count) noexcept {
        if (!values || count == 0) return 0;
        
        uint32_t dequeued = 0;
        for (uint32_t i = 0; i < count; ++i) {
            if (dequeue(values[i])) {
                dequeued++;
            } else {
                break;  // Stop at first failure
            }
        }
        return dequeued;
    }
    
    /**
     * @brief Peek at the next element without dequeuing (consumer thread only)
     * 
     * @param[out] value The next element (valid only if return is true)
     * @return true if an element is available, false if buffer is empty
     */
    bool peek(T& value) const noexcept {
        const uint32_t tail = consumer_tail_.load(std::memory_order_relaxed);
        const uint32_t head = producer_head_.load(std::memory_order_acquire);
        
        if (tail == head) {
            return false;  // Empty
        }
        
        value = data_[tail];
        return true;
    }
    
    /**
     * @brief Get the number of elements currently in the buffer
     * 
     * Note: This is a snapshot and may become stale due to concurrent access.
     * Should only be used for monitoring/debugging, not for logic decisions.
     * 
     * @return Approximate number of elements in buffer
     */
    uint32_t size() const noexcept {
        const uint32_t head = producer_head_.load(std::memory_order_acquire);
        const uint32_t tail = consumer_tail_.load(std::memory_order_acquire);
        
        if (head >= tail) {
            return head - tail;
        } else {
            return SIZE - (tail - head);
        }
    }
    
    /**
     * @brief Get available space in the buffer
     * 
     * Note: This is a snapshot and may become stale.
     * 
     * @return Approximate number of free slots
     */
    uint32_t available() const noexcept {
        return SIZE - size() - 1;  // -1 because full is when head == tail
    }
    
    /**
     * @brief Check if buffer is empty
     * 
     * @return true if buffer is empty
     */
    bool empty() const noexcept {
        return producer_head_.load(std::memory_order_acquire) ==
               consumer_tail_.load(std::memory_order_acquire);
    }
    
    /**
     * @brief Check if buffer is full
     * 
     * @return true if buffer is full
     */
    bool full() const noexcept {
        const uint32_t head = producer_head_.load(std::memory_order_acquire);
        const uint32_t tail = consumer_tail_.load(std::memory_order_acquire);
        return ((head + 1) & MASK) == tail;
    }
    
    /**
     * @brief Clear the buffer (should only be called when no activity)
     * 
     * This is NOT thread-safe during concurrent operations.
     * Should only be called during initialization or shutdown.
     */
    void clear() noexcept {
        producer_head_.store(0, std::memory_order_release);
        consumer_tail_.store(0, std::memory_order_release);
    }
    
    /**
     * @brief Get the capacity of this buffer
     * 
     * @return Fixed capacity SIZE
     */
    static constexpr uint32_t capacity() noexcept {
        return SIZE;
    }
    
private:
    // Data array
    std::unique_ptr<T[]> data_;
    
    // Producer head pointer - modified only by producer thread
    // Padded to cache line boundary to prevent false sharing
    alignas(CACHE_LINE) std::atomic<uint32_t> producer_head_;
    
    // Consumer tail pointer - modified only by consumer thread
    // Padded to cache line boundary to prevent false sharing
    alignas(CACHE_LINE) std::atomic<uint32_t> consumer_tail_;
};

/**
 * @class SPSCQueue
 * @brief High-level SPSC queue wrapper with thread roles explicit
 * 
 * This is an alternative API that makes it clearer which thread should
 * call which methods. Inherits from RingBuffer but provides separate
 * Producer and Consumer interfaces.
 */
template <typename T, uint32_t SIZE>
class SPSCQueue : public RingBuffer<T, SIZE> {
public:
    using Base = RingBuffer<T, SIZE>;
    
    /**
     * @brief Producer interface - only safe to call from producer thread
     */
    class Producer {
    public:
        explicit Producer(RingBuffer<T, SIZE>* queue) : queue_(queue) {}
        
        bool push(const T& value) noexcept {
            return queue_->enqueue(value);
        }
        
        uint32_t push_batch(const T* values, uint32_t count) noexcept {
            return queue_->enqueue_batch(values, count);
        }
        
        bool is_full() const noexcept {
            return queue_->full();
        }
        
        uint32_t available() const noexcept {
            return queue_->available();
        }
        
    private:
        RingBuffer<T, SIZE>* queue_;
    };
    
    /**
     * @brief Consumer interface - only safe to call from consumer thread
     */
    class Consumer {
    public:
        explicit Consumer(RingBuffer<T, SIZE>* queue) : queue_(queue) {}
        
        bool pop(T& value) noexcept {
            return queue_->dequeue(value);
        }
        
        uint32_t pop_batch(T* values, uint32_t count) noexcept {
            return queue_->dequeue_batch(values, count);
        }
        
        bool peek_front(T& value) const noexcept {
            return queue_->peek(value);
        }
        
        bool is_empty() const noexcept {
            return queue_->empty();
        }
        
        uint32_t size() const noexcept {
            return queue_->size();
        }
        
    private:
        RingBuffer<T, SIZE>* queue_;
    };
    
    /**
     * @brief Get producer interface for this queue
     * 
     * The returned Producer should only be used from the producer thread.
     */
    Producer get_producer() noexcept {
        return Producer(this);
    }
    
    /**
     * @brief Get consumer interface for this queue
     * 
     * The returned Consumer should only be used from the consumer thread.
     */
    Consumer get_consumer() noexcept {
        return Consumer(this);
    }
};

} // namespace hft::core