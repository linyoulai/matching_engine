#include <cstdint>

struct QueryRequest {
    uint64_t tag;        // 8 字节
    uint32_t trader_id;  // 4 字节
    uint32_t symbol_id;  // 4 字节
}; // 合计 16 字节，完美对齐

struct OrderRequest {

};

struct CancelRequest {
    /* data */
};


// 业务请求包
struct RequestEnvelope {
    uint8_t type; // 0: 下单, 1: 撤单, 2: 查单
    union {
        OrderRequest order_req;
        CancelRequest cancel_req;
        QueryRequest query_req;
    } data;
};

