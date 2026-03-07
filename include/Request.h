// include/Request.h
#include <cstdint>
#include "Order.h"

// 请求类型 
enum class RequestType : uint8_t {
    ORDER,
    CANCEL,
    QUERY
};

// 下单请求
struct OrderRequest {
    uint64_t tag;
    uint64_t ts;
    uint32_t symbol_id;
    uint32_t trader_id;
    int64_t price;
    uint32_t qty;
    // uint32_t filled_qty;
    Side side;
    OrderType order_type;
    TimeInForce tif;
    // OrderStatus order_status;
};

// 撤单请求
struct CancelRequest {
    uint64_t order_id;        // 8 字节
    uint32_t symbol_id;  // 4 字节
    uint32_t trader_id;  // 4 字节
};

// 查单请求
struct QueryRequest {
    uint64_t order_id;        // 8 字节
    uint32_t symbol_id;  // 4 字节
    uint32_t trader_id;  // 4 字节
}; // 合计 16 字节，完美对齐

// 业务请求包
struct RequestEnvelope {
    RequestType req_type; // 0: 下单, 1: 撤单, 2: 查单
    union {
        OrderRequest order_req;
        CancelRequest cancel_req;
        QueryRequest query_req;
    } data;
};

