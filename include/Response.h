// include/Response.h
/*
3.响应(Response):
    HTTP接口响应：
    成交回报：
    行情推送：
*/
#pragma once
#include <cstdint>
#include "Order.h"

enum class ResponseType : uint8_t {
    TRADE_RESPONSE,
    MARKET_DATA_SNAPSHOT
};

// 成交回报应包含字段：response_type, order_id, trader_id, symbol_id, side, filled_qty, price, status
struct TradeResponse {
    ResponseType response_type = ResponseType::TRADE_RESPONSE;
    uint64_t order_id;
    uint32_t trader_id;
    uint32_t symbol_id;
    Side side;
    int64_t price;
    uint32_t filled_qty;
    OrderStatus status;
};

// 行情推送应包含字段：response_type, symbol_id, timestamp, bids(price, qty), asks(price, qty).
struct MarketDataResponse {
    ResponseType response_type = ResponseType::MARKET_DATA_SNAPSHOT;
    uint32_t symbol_id;
    uint64_t timestamp;
    struct Level {
        int64_t price;
        uint32_t qty;
    };
    Level bids[5]; // 从小到大排列
    Level asks[5]; // 从小到大排列
};
