// include/Order.h
#include <cstdint>

// 买卖方向
enum class Side : uint8_t {
    BUY,
    SELL
};

// 订单类型枚举（OrderType）：包括限价单（LIMIT），市价单（MARKET）。
enum class OrderType : uint8_t {    
    LIMIT,
    MARKET
};

// TimeInForce：tif包括GTC，IOC，FOK。
enum class TimeInForce : uint8_t {
    GTC,
    IOC,
    FOK
};

// 订单状态（OrderStatus）：NEW，EXPIRED，PARTIAL_FILLED，FILLED，REJECTED。
enum class OrderStatus : uint8_t {
    NEW,
    PARTIAL_FILLED,
    FILLED, 
    EXPIRED,
    REJECTED,
    CANCELED
};

// order_id（直接用网关层打的tag），ts，symbol_id, trader_id, price，qty，filled_qty, side, order_type, tif, order_status
struct Order {
    uint64_t order_id;
    uint64_t ts;
    uint32_t symbol_id;
    uint32_t trader_id;
    int64_t price;
    uint32_t qty;
    uint32_t filled_qty;
    Side side;
    OrderType order_type;
    TimeInForce tif;
    OrderStatus order_status;
};

