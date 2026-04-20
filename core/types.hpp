// types.hpp
// struct Tick;
// struct Order;
// struct Trade;
// struct LOBLevel;

#pragma once

#include <cstdint>
#include <cstring>
#include <chrono>
#include <iostream>

namespace hft::core {

// ============================================================================
// Type Aliases for Consistency and Clarity
// ============================================================================

using Price = double;           // Price in decimal format (e.g., 100.25)
using Quantity = uint64_t;      // Order quantity in shares
using OrderId = uint64_t;       // Unique order identifier
using TradeId = uint64_t;       // Unique trade identifier
using Timestamp = int64_t;     // Nanosecond precision timestamp

// ============================================================================
// Enums for Market Data and Order States
// ============================================================================

enum class Side : uint8_t {
    BUY = 0,
    SELL = 1,
    UNKNOWN = 2
};

enum class OrderStatus : uint8_t {
    PENDING = 0,
    ACCEPTED = 1,
    PARTIAL_FILL = 2,
    FILLED = 3,
    CANCELLED = 4,
    REJECTED = 5,
    UNKNOWN = 6
};

enum class OrderType : uint8_t {
    LIMIT = 0,
    MARKET = 1,
    STOP = 2,
    UNKNOWN = 3
};

// ============================================================================
// Core Data Structures - Optimized for Cache Efficiency
// ============================================================================

/**
 * @struct Tick
 * @brief Represents a single market data update (ask/bid quote or trade)
 * 
 * Layout optimized for cache locality:
 * - Timestamp first (frequently accessed)
 * - Numeric fields grouped
 * - Enum fields (smaller types) grouped together
 * 
 * Size: 48 bytes (fits within typical 64-byte cache line with padding)
 */
struct alignas(64) Tick {
    Timestamp timestamp;         // 8 bytes - When this tick occurred
    Price price;                 // 8 bytes - Price level
    Quantity volume;             // 8 bytes - Volume at this level
    uint32_t sequence;           // 4 bytes - Message sequence number
    Side side;                   // 1 byte  - Buy or Sell
    uint8_t is_trade;            // 1 byte  - Whether this is an executed trade
    uint8_t padding[6];          // 6 bytes - Padding for alignment
    
    Tick() : timestamp(0), price(0.0), volume(0), sequence(0), 
             side(Side::UNKNOWN), is_trade(0) {}
    
    Tick(Timestamp ts, Price p, Quantity v, Side s, bool trade = false)
        : timestamp(ts), price(p), volume(v), sequence(0),
          side(s), is_trade(trade ? 1 : 0) {}
    
    // Implicit conversion to check if valid
    explicit operator bool() const {
        return timestamp != 0 && volume > 0;
    }
};

static_assert(sizeof(Tick) == 64, "Tick must be 64 bytes (one cache line)");
static_assert(alignof(Tick) == 64, "Tick must be cache-line aligned");

/**
 * @struct Trade
 * @brief Details of an executed trade
 * 
 * Contains both sides of the trade for record-keeping and reconciliation.
 * Size: 64 bytes - aligned to cache line
 */
struct alignas(64) Trade {
    Timestamp timestamp;         // 8 bytes - When trade was executed
    TradeId trade_id;            // 8 bytes - Unique trade identifier
    OrderId buyer_order_id;      // 8 bytes - Buyer's order ID
    OrderId seller_order_id;     // 8 bytes - Seller's order ID
    Price trade_price;           // 8 bytes - Execution price
    Quantity trade_quantity;     // 8 bytes - Quantity traded
    uint16_t padding[2];         // 4 bytes - Padding for alignment
    
    Trade() : timestamp(0), trade_id(0), buyer_order_id(0),
              seller_order_id(0), trade_price(0.0), trade_quantity(0) {}
    
    Trade(Timestamp ts, TradeId tid, OrderId buy_id, OrderId sell_id,
          Price price, Quantity qty)
        : timestamp(ts), trade_id(tid), buyer_order_id(buy_id),
          seller_order_id(sell_id), trade_price(price), trade_quantity(qty) {}
};

static_assert(sizeof(Trade) == 64, "Trade must be 64 bytes");
static_assert(alignof(Trade) == 64, "Trade must be cache-line aligned");

/**
 * @struct Order
 * @brief Represents a trading order placed or tracked by the system
 * 
 * Lightweight representation of order state. Full order details would be
 * maintained in a separate order book structure for cache efficiency.
 * Size: 64 bytes - aligned to cache line
 */
struct alignas(64) Order {
    OrderId order_id;            // 8 bytes - Unique order identifier
    Timestamp creation_time;     // 8 bytes - When order was created
    Timestamp last_update;       // 8 bytes - Last state change timestamp
    Price price;                 // 8 bytes - Limit price
    Quantity quantity;           // 8 bytes - Total quantity
    Quantity filled;             // 8 bytes - Already filled quantity
    
    Side side;                   // 1 byte  - Buy or Sell
    OrderType type;              // 1 byte  - Limit, Market, etc.
    OrderStatus status;          // 1 byte  - Current order status
    uint8_t padding[5];          // 5 bytes - Padding for alignment
    
    Order() : order_id(0), creation_time(0), last_update(0),
              price(0.0), quantity(0), filled(0),
              side(Side::UNKNOWN), type(OrderType::LIMIT),
              status(OrderStatus::PENDING) {}
    
    Order(OrderId id, Side s, OrderType t, Price p, Quantity q)
        : order_id(id), creation_time(0), last_update(0),
          price(p), quantity(q), filled(0),
          side(s), type(t), status(OrderStatus::PENDING) {}
    
    // Calculate remaining quantity to fill
    Quantity remaining() const {
        return quantity - filled;
    }
    
    // Check if order is fully filled
    bool is_filled() const {
        return filled >= quantity;
    }
    
    // Get fill percentage (0-100)
    double fill_percentage() const {
        return quantity > 0 ? (static_cast<double>(filled) / quantity) * 100.0 : 0.0;
    }
};

static_assert(sizeof(Order) == 64, "Order must be 64 bytes");
static_assert(alignof(Order) == 64, "Order must be cache-line aligned");

/**
 * @struct LOBLevel
 * @brief A single price level in the Limit Order Book (LOB)
 * 
 * Represents aggregated volume at a specific price level.
 * Designed for efficient traversal during market data processing.
 * Size: 32 bytes
 */
struct alignas(32) LOBLevel {
    Price price;                 // 8 bytes - Price level
    Quantity volume;             // 8 bytes - Total volume at this level
    uint32_t order_count;        // 4 bytes - Number of orders at this level
    uint32_t padding;            // 4 bytes - Padding for alignment
    
    LOBLevel() : price(0.0), volume(0), order_count(0) {}
    
    LOBLevel(Price p, Quantity v, uint32_t count = 1)
        : price(p), volume(v), order_count(count) {}
    
    explicit operator bool() const {
        return volume > 0;
    }
};

static_assert(sizeof(LOBLevel) == 32, "LOBLevel must be 32 bytes");
static_assert(alignof(LOBLevel) == 32, "LOBLevel must be 32-byte aligned");

/**
 * @struct LOBSnapshot
 * @brief A snapshot of the market's current best bid/ask
 * 
 * Minimal representation for ultra-fast market data processing.
 * Size: 64 bytes
 */
struct alignas(64) LOBSnapshot {
    Timestamp timestamp;         // 8 bytes
    Price best_bid;              // 8 bytes
    Price best_ask;              // 8 bytes
    Quantity bid_volume;         // 8 bytes
    Quantity ask_volume;         // 8 bytes
    uint32_t bid_count;          // 4 bytes
    uint32_t ask_count;          // 4 bytes
    uint8_t padding[8];          // 8 bytes - Padding for alignment
    
    LOBSnapshot() : timestamp(0), best_bid(0.0), best_ask(0.0),
                    bid_volume(0), ask_volume(0), bid_count(0), ask_count(0) {}
    
    // Calculate mid price (average of bid and ask)
    Price mid_price() const {
        return (best_bid + best_ask) / 2.0;
    }
    
    // Calculate spread
    Price spread() const {
        return best_ask - best_bid;
    }
    
    // Check if quote is valid
    explicit operator bool() const {
        return best_bid > 0 && best_ask > best_bid;
    }
};

static_assert(sizeof(LOBSnapshot) == 64, "LOBSnapshot must be 64 bytes");
static_assert(alignof(LOBSnapshot) == 64, "LOBSnapshot must be cache-line aligned");

/**
 * @struct MarketStatistics
 * @brief Rolling statistics for market data (used by feature engines)
 * 
 * Maintains volatile market metrics for real-time analysis.
 * Size: 64 bytes
 */
struct alignas(64) MarketStatistics {
    Timestamp timestamp;         // 8 bytes
    double returns;              // 8 bytes - Price returns
    double volatility;           // 8 bytes - Realized volatility
    double volume_wma;           // 8 bytes - Weighted moving average of volume
    int64_t total_trades;        // 8 bytes - Total trades in period
    uint32_t tick_count;         // 4 bytes - Number of ticks processed
    uint32_t padding;            // 4 bytes - Padding for alignment
    
    MarketStatistics() : timestamp(0), returns(0.0), volatility(0.0),
                         volume_wma(0.0), total_trades(0), tick_count(0) {}
};

static_assert(sizeof(MarketStatistics) == 64, "MarketStatistics must be 64 bytes");

// ============================================================================
// Utility Functions
// ============================================================================

inline std::ostream& operator<<(std::ostream& os, Side side) {
    return os << (side == Side::BUY ? "BUY" : side == Side::SELL ? "SELL" : "UNKNOWN");
}

inline std::ostream& operator<<(std::ostream& os, OrderStatus status) {
    switch (status) {
        case OrderStatus::PENDING: return os << "PENDING";
        case OrderStatus::ACCEPTED: return os << "ACCEPTED";
        case OrderStatus::PARTIAL_FILL: return os << "PARTIAL_FILL";
        case OrderStatus::FILLED: return os << "FILLED";
        case OrderStatus::CANCELLED: return os << "CANCELLED";
        case OrderStatus::REJECTED: return os << "REJECTED";
        default: return os << "UNKNOWN";
    }
}

inline std::ostream& operator<<(std::ostream& os, OrderType type) {
    switch (type) {
        case OrderType::LIMIT: return os << "LIMIT";
        case OrderType::MARKET: return os << "MARKET";
        case OrderType::STOP: return os << "STOP";
        default: return os << "UNKNOWN";
    }
}

inline std::ostream& operator<<(std::ostream& os, const Tick& tick) {
    os << "Tick{ts=" << tick.timestamp << ", px=" << tick.price 
       << ", vol=" << tick.volume << ", side=" << tick.side
       << ", trade=" << (tick.is_trade ? "true" : "false") << "}";
    return os;
}

inline std::ostream& operator<<(std::ostream& os, const Order& order) {
    os << "Order{id=" << order.order_id << ", " << order.side << ", "
       << order.type << ", px=" << order.price << ", qty=" << order.quantity
       << ", filled=" << order.filled << ", " << order.status << "}";
    return os;
}

} // namespace hft::core