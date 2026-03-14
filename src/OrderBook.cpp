#include "OrderBook.h"
#include <spdlog/spdlog.h>

OrderBook::OrderBook(int64_t prev_close_price) { // 默认前收盘价为100.00元，即10000分
    upper_limit_price = static_cast<int64_t>(std::round(prev_close_price * 1.1));
    lower_limit_price = static_cast<int64_t>(std::round(prev_close_price * 0.9));
    old_best_ask_price = -1;
    old_best_bid_price = -1;

    size_t book_length = static_cast<size_t>(upper_limit_price - lower_limit_price + 1); // 订单簿长度
    bid_book.resize(book_length);
    ask_book.resize(book_length);

    order_map.reserve(1000);
}

void OrderBook::add_order(const Order& order) {
    // 加入订单簿
    size_t index = price_to_index(order.price);
    std::list<Order>& target_list = (order.side == Side::BUY) ? bid_book[index] : ask_book[index];
    target_list.push_back(order);
    // 记录位置
    order_map[order.order_id] = { order.side, order.price, std::prev(target_list.end()) };
    // 更新最优价
    if (order.side == Side::BUY) {
        if (old_best_bid_price == -1 || order.price > old_best_bid_price) {
            old_best_bid_price = order.price;
        }
    } else {
        if (old_best_ask_price == -1 || order.price < old_best_ask_price) {
            old_best_ask_price = order.price;
        }
    }
}

bool OrderBook::cancel_order(uint64_t order_id) {
    auto it = order_map.find(order_id);
    if (it == order_map.end()) {
        spdlog::debug("没找到订单, 撤单失败");
        return false; // 订单不存在，可能已经成交了
    }
    const OrderLocation& loc = it->second;
    size_t index = price_to_index(loc.price);
    std::list<Order>& target_list = (loc.side == Side::BUY) ? bid_book[index] : ask_book[index];
    target_list.erase(loc.it); // 从订单簿中删除订单
    order_map.erase(it); // 从映射中删除订单
    return true;
}

void OrderBook::remove_order(uint64_t order_id) {
    OrderLocation loc = order_map[order_id];
    std::vector<std::list<Order>> target_book = loc.side == Side::BUY ? bid_book : ask_book;
    target_book[price_to_index(loc.price)].erase(loc.it); // 从订单簿中删除订单
    order_map.erase(order_id); // 从映射中删除订单
    spdlog::debug("订单已成交, 已从订单簿中删除订单");
}