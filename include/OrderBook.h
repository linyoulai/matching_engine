// include/OrderBook.h
/*
4.订单簿（OrderBook）：
    用vector+list+unordered_map实现，price(int64_t)为索引，price的精度为0.01元，考虑到A股的涨跌停板一般在10%，所以价格跳跃不会很大，所以vector不会占用很多内存。开盘前加载昨日收盘价，乘1.1设置为涨停板，乘0.9设置为跌停板。
    前收盘价(prev_close_price)：计算公式有些复杂，暂时设置为一个定值。
    今日涨停板：前收盘价 * 11 / 10
    今日跌停板：前收盘价 * 9 / 10
    索引 = price - lower_limit_price
    price相同时用list存储订单：
        1.撮合引擎插入：直接在list尾部插入。
        2.撮合引擎取出：直接从list首部取出。
        3.撮合引擎撤单：用unordered_map快速查询，如果查到了，就撤单。如果没有查到，说明已经成交了，就返回REJECTED。
    用unordered_map存储每个订单对象的迭代器，用于快速撤单和查单。
*/
#pragma once
#include <vector>
#include <list>
#include <unordered_map>
#include <cstddef>
#include <stdexcept>
#include <cmath>
#include "Order.h"

struct OrderLocation {
    Side side; // 买卖方向
    int64_t price; // 价格
    std::list<Order>::iterator it; // 订单在list中的迭代器
};

class OrderBook {
private:
    int64_t upper_limit_price; // 涨停板价格，单位为分
    int64_t lower_limit_price; // 跌停板价格
public:
    std::unordered_map<uint64_t, OrderLocation> order_map; // 订单ID到订单迭代器的映射
    std::vector<std::list<Order>> bid_book; // 买单簿
    std::vector<std::list<Order>> ask_book; // 卖单簿

public:
    explicit OrderBook(int64_t prev_close_price = 10000) { // 默认前收盘价为100.00元，即10000分
        upper_limit_price = static_cast<int64_t>(std::round(prev_close_price * 1.1));
        lower_limit_price = static_cast<int64_t>(std::round(prev_close_price * 0.9));

        size_t book_length = static_cast<size_t>(upper_limit_price - lower_limit_price + 1); // 订单簿长度
        bid_book.resize(book_length);
        ask_book.resize(book_length);

        order_map.reserve(1000);
    }

    int64_t get_best_bid() const {
        for (size_t i = bid_book.size() - 1; i >= 0; --i) {
            if (!bid_book[i].empty()) {
                return lower_limit_price + static_cast<int64_t>(i);
            }
        }
        return -1; // 没有买单
    }

    int64_t get_best_ask() const {
        for (size_t i = 0; i < ask_book.size(); ++i) {
            if (!ask_book[i].empty()) {
                return lower_limit_price + static_cast<int64_t>(i);
            }
        }
        return -1; // 没有卖单
    }

    void add_order(const Order& order) {
        // 加入订单簿
        size_t index = price_to_index(order.price);
        std::list<Order>& target_list = (order.side == Side::BUY) ? bid_book[index] : ask_book[index];
        target_list.push_back(order);
        // 记录位置
        order_map[order.order_id] = { order.side, order.price, std::prev(target_list.end()) };
    }

    bool cancel_order(uint64_t order_id) {
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

    void remove_order(uint64_t order_id) {
        OrderLocation loc = order_map[order_id];
        std::vector<std::list<Order>> target_book = loc.side == Side::BUY ? bid_book : ask_book;
        target_book[price_to_index(loc.price)].erase(loc.it); // 从订单簿中删除订单
        order_map.erase(order_id); // 从映射中删除订单
        spdlog::debug("订单已成交, 已从订单簿中删除订单");
    }

    // 将价格转换为订单簿索引
    inline size_t price_to_index(int64_t price) const {
        if (price < lower_limit_price) [[unlikely]] {
            throw std::out_of_range("Price is below lower limit");
        }
        if (price > upper_limit_price) [[unlikely]] {
            throw std::out_of_range("Price is above upper limit");
        }
        return (size_t)(price - lower_limit_price);
    }

    int64_t get_upper_limit_price() const {
        return upper_limit_price;
    }

    int64_t get_lower_limit_price() const {
        return lower_limit_price;
    }
};