#pragma once
#include <string>
#include <unordered_map>
#include <functional>
#include <vector>
#include <cstdint>
#include "Gateway.h"
#include "MarketDataService.h"

// WebSocket消息类型
enum class WebSocketMsgType {
    REQUEST,
    RESPONSE,
    SUBSCRIBE,
    UNSUBSCRIBE,
    MARKET_DATA,
    HEARTBEAT,
    ERROR
};

// WebSocket消息结构体
struct WebSocketMessage {
    WebSocketMsgType type;
    std::string session_id;
    std::string payload; // JSON或二进制
    uint64_t timestamp;
};

// WebSocket连接信息
struct WebSocketSession {
    std::string session_id;
    uint64_t user_id;
    std::string client_ip;
    uint64_t connect_time;
    bool authenticated;
    // 可扩展：订阅信息、权限等
};

// WebSocket接口服务
class WebSocketApiService {
public:
    explicit WebSocketApiService();
    // 连接/断开
    void onConnect(const WebSocketSession& session);
    void onDisconnect(const std::string& session_id);
    // 收到消息
    void onMessage(const WebSocketMessage& msg);
    // 发送消息
    void sendMessage(const std::string& session_id, const WebSocketMessage& msg);
    // 广播消息
    void broadcast(const WebSocketMessage& msg);
    // 订阅/取消订阅行情
    void subscribeMarketData(const std::string& session_id, MarketDataType type, const std::string& symbol);
    void unsubscribeMarketData(const std::string& session_id, MarketDataType type, const std::string& symbol);
    // 认证回调
    void setAuthCallback(const std::function<bool(const WebSocketSession&, const WebSocketMessage&)>& cb);
    // 日志回调
    void setLogCallback(const std::function<void(const std::string&)>& cb);
    void log(const std::string& msg) const;
private:
    std::unordered_map<std::string, WebSocketSession> sessions_;
    // ...
};
