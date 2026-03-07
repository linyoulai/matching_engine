#pragma once
#include <string>
#include <cstdint>

// Enum for order types
enum class OrderType {
    LIMIT, 
    MARKET
    // STOP orders can be handled by trigger logic, not as a type
};

// Time in force: order validity
enum class TimeInForce {
    GTC, // Good Till Cancelled
    IOC, // Immediate Or Cancel
    FOK, // Fill Or Kill
    DAY, // Day order
    GTD  // Good Till Date
};

// Enum for order status
enum class OrderStatus {
    NEW,
    PARTIALLY_FILLED,
    FILLED,
    CANCELLED,
    REJECTED
};

struct Order {
    uint64_t order_id;
    uint64_t timestamp;
    bool is_buy;
    int trader_id;
    int price;
    int quantity;
    OrderType type;
    TimeInForce tif;
    OrderStatus status;
    // Constructor and other members (to be implemented)
};
