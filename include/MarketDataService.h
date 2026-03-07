#pragma once
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <cstdint>


// 行情类型
enum class MarketDataType {
    ORDER_BOOK_SNAPSHOT, // 订单簿快照
    ORDER_BOOK_DELTA,   // 订单簿增量
    TRADE,              // 逐笔成交
    TICKER,             // 最新价、成交量等
    DEPTH,              // 深度行情
    INDEX,              // 指数行情
    AUCTION,            // 集合竞价行情
    STATISTICS,         // 统计数据
    NEWS                // 资讯公告
    // 可扩展更多类型
};

// 行情推送QoS等级
enum class MarketDataQoS {
    AT_MOST_ONCE,   // 最多一次（不保证送达）
    AT_LEAST_ONCE,  // 至少一次（可能重复）
    EXACTLY_ONCE    // 精确一次（需回执）
};



// 订单簿快照结构体
struct OrderBookSnapshot {
    std::string symbol;
    uint64_t timestamp;
    std::vector<std::pair<double, int>> bids; // 价格、数量
    std::vector<std::pair<double, int>> asks;
    int level = 10; // 档位数
    std::string exchange;
    // 可扩展：撮合状态、盘口统计等
};

// 订单簿增量结构体
struct OrderBookDelta {
    std::string symbol;
    uint64_t timestamp;
    std::vector<std::pair<double, int>> bid_updates;
    std::vector<std::pair<double, int>> ask_updates;
    // 可扩展：撤单、改单等
};


// 逐笔成交结构体
struct TradeTick {
    std::string symbol;
    uint64_t timestamp;
    double price;
    int quantity;
    int buy_trader_id;
    int sell_trader_id;
    std::string trade_id;
    std::string side; // "buy"/"sell"
    std::string exchange;
    // 可扩展：成交类型、撮合方式等
};

// Ticker结构体
struct Ticker {
    std::string symbol;
    uint64_t timestamp;
    double last_price;
    double open;
    double high;
    double low;
    double close;
    double volume;
    // 可扩展：涨跌幅、换手率等
};

// 深度行情结构体
struct Depth {
    std::string symbol;
    uint64_t timestamp;
    std::vector<std::pair<double, int>> depth_bids;
    std::vector<std::pair<double, int>> depth_asks;
    // 可扩展：逐笔撤单、逐笔改单等
};

// 指数行情结构体
struct IndexData {
    std::string index_code;
    uint64_t timestamp;
    double value;
    double change;
    // 可扩展：成分股、权重等
};

// 集合竞价行情结构体
struct AuctionData {
    std::string symbol;
    uint64_t timestamp;
    double auction_price;
    int auction_volume;
    // 可扩展：阶段、状态等
};

// 统计数据结构体
struct StatisticsData {
    std::string symbol;
    uint64_t timestamp;
    double avg_price;
    double vwap;
    // 可扩展：分时统计、区间统计等
};

// 资讯公告结构体
struct NewsData {
    std::string symbol;
    uint64_t timestamp;
    std::string title;
    std::string content;
    // 可扩展：公告类型、优先级等
};

#include <variant>
using MarketDataPayload = std::variant<
    OrderBookSnapshot,
    OrderBookDelta,
    TradeTick,
    Ticker,
    Depth,
    IndexData,
    AuctionData,
    StatisticsData,
    NewsData,
    std::string // 兼容自定义/扩展
>;

struct MarketData {

    MarketDataType type;
    std::string symbol;
    uint64_t timestamp;
    MarketDataPayload payload; // 类型安全的行情内容
};

// 推送回执结构体，移到类外部
struct MarketDataAck {
    uint64_t user_id;
    std::string symbol;
    MarketDataType type;
    uint64_t data_timestamp;
    bool success;
    uint64_t ack_timestamp;
    std::string error_message;
};

// 行情推送服务
class MarketDataService {
public:
    explicit MarketDataService();
    // 订阅行情，支持推送模式（推/拉）、过滤条件、批量订阅
        // 订阅优先级
    enum class SubscriptionPriority {
        LOW,
        NORMAL,
        HIGH,
        CRITICAL
    };

    // 订阅生命周期管理
    enum class SubscriptionStatus {
        ACTIVE,
        PAUSED,
        EXPIRED,
        CANCELLED
    };

    struct SubscriptionInfo {
        uint64_t user_id;
        MarketDataType type;
        std::string symbol;
        SubscriptionPriority priority;
        SubscriptionStatus status;
        uint64_t subscribe_time;
        uint64_t last_active_time;
        bool push_mode;
        std::string filter;
    };

    void subscribe(uint64_t user_id, MarketDataType type, const std::string& symbol,
                    const std::function<void(const MarketData&)>& callback,
                    bool push_mode = true,
                    const std::string& filter = "",
                    SubscriptionPriority priority = SubscriptionPriority::NORMAL,
                    MarketDataQoS qos = MarketDataQoS::AT_MOST_ONCE);
    // 取消订阅，支持批量取消
    void unsubscribe(uint64_t user_id, MarketDataType type, const std::string& symbol);
    void unsubscribeAll(uint64_t user_id);
    // 推送单条行情（由撮合引擎/订单簿调用）
    // 推送行情，支持QoS、压缩、加密等参数
    void publish(const MarketData& data,
                 MarketDataQoS qos = MarketDataQoS::AT_MOST_ONCE,
                 bool compress = false,
                 bool encrypt = false);
    // 批量推送行情
    void publishBatch(const std::vector<MarketData>& data_vec,
                      MarketDataQoS qos = MarketDataQoS::AT_MOST_ONCE,
                      bool compress = false,
                      bool encrypt = false);
    // 查询当前订阅用户
    std::vector<uint64_t> getSubscribers(MarketDataType type, const std::string& symbol) const;
    // 快照重发（如用户断线重连），支持重发策略和QoS
    void resendSnapshot(uint64_t user_id, const std::string& symbol, int strategy = 0, MarketDataQoS qos = MarketDataQoS::AT_MOST_ONCE);
    // 权限校验回调
    void setPermissionCallback(const std::function<bool(uint64_t user_id, MarketDataType, const std::string&)>& cb);
    // 日志回调
    void setLogCallback(const std::function<void(const std::string&)>& cb);
    void log(const std::string& msg) const;
    // 推送统计接口
    size_t getPushCount(uint64_t user_id) const;
    size_t getTotalPushCount() const;
    // 推送统计与监控
    double getAvgPushDelay(uint64_t user_id) const;
    double getMaxPushDelay(uint64_t user_id) const;
    // 订阅信息查询
    std::vector<SubscriptionInfo> listSubscriptions(uint64_t user_id) const;
    SubscriptionInfo getSubscriptionInfo(uint64_t user_id, MarketDataType type, const std::string& symbol) const;
    // 推送回执处理
    void handleAck(const MarketDataAck& ack);
    // ...可扩展更多接口
private:
    // 订阅关系表
    // user_id -> (type, symbol) -> callback
    std::unordered_map<uint64_t, std::unordered_map<std::string, std::function<void(const MarketData&)>>> subscriptions_;
    // 权限校验
    std::function<bool(uint64_t, MarketDataType, const std::string&)> permission_cb_;
    // 推送统计
    std::unordered_map<uint64_t, size_t> user_push_count_;
    size_t total_push_count_ = 0;
    // ...
};
