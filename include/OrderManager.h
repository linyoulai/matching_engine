#pragma once
#include "Order.h"
#include <unordered_map>
#include <vector>
#include <optional>

// OrderManager: Manages the lifecycle of all orders
class OrderManager {
public:
    explicit OrderManager();

    // Add a new order, returns assigned order_id
    uint64_t addOrder(const Order& order);

    // Cancel an order by order_id, returns true if successful
    bool cancelOrder(uint64_t order_id);

    // Modify an order (to be implemented as needed)
    // bool modifyOrder(uint64_t order_id, ...);

    // Get order by order_id
    std::optional<Order> getOrder(uint64_t order_id) const;

    // Get all active orders
    std::vector<Order> getActiveOrders() const;

private:
    std::unordered_map<uint64_t, Order> active_orders_;
    uint64_t next_order_id_ = 1;
    // Optionally: store historical orders, event callbacks, etc.
};
