#pragma once
#include "OrderBook.h"
#include <unordered_map>
#include <vector>
#include <memory>

// The core matching engine, managing all order books and orchestrating matching
class MatchingEngine {
public:
    MatchingEngine();
    // Add order, cancel order, process all orders, etc. (to be implemented)
    // void addOrder(const Order& order);
    // void cancelOrder(uint64_t order_id);
    // ...
private:
    // One order book per symbol/instrument (to be implemented)
    std::unordered_map<std::string, std::unique_ptr<OrderBook>> order_books_;
    // Other engine state (to be implemented)
};
