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
    int64_t old_best_ask_price; // 卖一价
    int64_t old_best_bid_price; // 买一价
public:
    std::unordered_map<uint64_t, OrderLocation> order_map; // 订单ID到订单迭代器的映射
    std::vector<std::list<Order>> bid_book; // 买单簿
    std::vector<std::list<Order>> ask_book; // 卖单簿

public:
    explicit OrderBook(int64_t prev_close_price = 10000);
    ~OrderBook() = default;

    int64_t get_upper_limit_price() const {
        return upper_limit_price;
    }

    int64_t get_lower_limit_price() const {
        return lower_limit_price;
    }

    inline int64_t get_old_best_bid() const {
        return old_best_bid_price;
    }

    inline void set_old_best_bid(const int64_t price) {
        old_best_bid_price = price;
    }

    inline int64_t get_old_best_ask() const {
        return old_best_ask_price;
    }

    inline void set_old_best_ask(const int64_t price) {
        old_best_ask_price = price;
    }

    void add_order(const Order& order);

    bool cancel_order(uint64_t order_id);

    void remove_order(uint64_t order_id);

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
};