#pragma once
#include "Gateway.h"
#include <string>
#include <functional>
#include <unordered_map>


// HTTP方法枚举
enum class HttpMethod {
    GET,
    POST,
    PUT,
    DELETE_,
    PATCH,
    OPTIONS,
    HEAD
    // 可扩展更多方法
};

// RESTful API请求结构体
struct RestRequest {
    HttpMethod method;
    std::string path;   // "/order", "/cancel", etc.
    std::string body;   // JSON或其他格式
    std::unordered_map<std::string, std::string> headers;
    std::string client_ip;
    std::string auth_token; // 认证token
    std::string user_agent; // 客户端信息
    uint64_t request_time;  // 请求时间戳
    // 批量请求支持
    std::vector<std::string> batch_bodies; // 批量body
};

// RESTful API响应结构体
struct RestResponse {
    int status_code;    // 200, 400, 500, etc.
    std::string body;   // JSON或其他格式
    std::unordered_map<std::string, std::string> headers;
    std::string error_code;   // 业务错误码
    std::string error_message;// 详细错误信息
    bool success = true;
    // 批量响应支持
    std::vector<std::string> batch_bodies;
};

// RESTful接口服务
class RestApiService {
public:
    explicit RestApiService();
    // 处理RESTful请求，返回响应
    RestResponse handleRequest(const RestRequest& req);
    // 注册自定义路由
    void registerRoute(const std::string& path, HttpMethod method,
                      const std::function<RestResponse(const RestRequest&)>& handler);
    // 认证回调
    void setAuthCallback(const std::function<bool(const RestRequest&)>& cb);
    // 限流回调
    void setRateLimitCallback(const std::function<bool(const RestRequest&)>& cb);
    // 异常处理回调
    void setExceptionCallback(const std::function<void(const std::exception&, const RestRequest&)>& cb);
    // 日志回调
    void setLogCallback(const std::function<void(const std::string&)>& cb);
    void log(const std::string& msg) const;
};
