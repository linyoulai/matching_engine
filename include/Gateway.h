#pragma once
#include <cstdint>
#include <string>
#include <memory>
#include <functional>

// 请求类型
enum class GatewayRequestType {
    NEW_ORDER,         // 新订单
    CANCEL_ORDER,      // 撤单
    MODIFY_ORDER,      // 修改订单
    QUERY_ORDER,       // 查询订单
    QUERY_ACCOUNT,     // 查询账户信息
    BATCH_NEW_ORDER,   // 批量下单
    BATCH_CANCEL_ORDER,// 批量撤单
    HEARTBEAT,         // 心跳检测
    LOGIN,             // 用户登录
    LOGOUT             // 用户登出
};


// 各类请求参数结构体
struct NewOrderParam {
    // 订单相关参数
    // ...
};
struct CancelOrderParam {
    uint64_t order_id;
};
struct ModifyOrderParam {
    uint64_t order_id;
    // 可扩展：新价格、新数量等
};
struct QueryOrderParam {
    uint64_t order_id;
};
struct QueryAccountParam {
    uint64_t account_id;
};
struct BatchNewOrderParam {
    std::vector<NewOrderParam> orders;
};
struct BatchCancelOrderParam {
    std::vector<uint64_t> order_ids;
};
struct HeartbeatParam {};
struct LoginParam {
    std::string username;
    std::string password;
};
struct LogoutParam {
    uint64_t user_id;
};

#include <variant>

using GatewayPayload = std::variant<
    NewOrderParam,
    CancelOrderParam,
    ModifyOrderParam,
    QueryOrderParam,
    QueryAccountParam,
    BatchNewOrderParam,
    BatchCancelOrderParam,
    HeartbeatParam,
    LoginParam,
    LogoutParam
>;

struct GatewayRequest {
    uint64_t request_id;      // 全局唯一请求ID
    uint64_t user_id;         // 用户ID
    GatewayRequestType type;  // 请求类型
    uint64_t timestamp;       // 接收到请求的时间戳
    GatewayPayload payload;   // 类型安全的参数
};

// 网关模块接口
class Gateway {
public:
    explicit Gateway();
    // 接收外部请求，打标签并入队
    void receiveRequest(const GatewayRequest& request);
    // 从无锁队列取出下一个请求（由撮合引擎调用）
    bool fetchNextRequest(GatewayRequest& out_request);
    // 队列长度等监控接口
    size_t queueSize() const;
    // 风控接口：设置风控回调或策略
    void setRiskControlCallback(const std::function<bool(const GatewayRequest&)>& cb);
    // 限流接口：设置限流回调或策略
    void setRateLimitCallback(const std::function<bool(const GatewayRequest&)>& cb);
    // 日志接口：设置日志回调
    void setLogCallback(const std::function<void(const std::string&)>& cb);
    // 触发日志
    void log(const std::string& msg) const;
private:
    // 无锁队列智能指针（建议使用std::unique_ptr，类型由具体无锁队列库决定）
    std::unique_ptr<void, void(*)(void*)> lockfree_queue_;
    // 说明：实际项目中应替换为具体无锁队列类型的智能指针，如std::unique_ptr<LockFreeQueue<GatewayRequest>>。
    // 若必须用原生指针，需确保生命周期和线程安全，并有详细注释说明。
    // 其他状态
};
