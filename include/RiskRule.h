#pragma once
#include "Gateway.h"
#include <string>
#include <memory>

// 风控规则基类
class RiskRule {
public:
    virtual ~RiskRule() = default;
    // 校验请求，reason为失败原因
    virtual bool check(const GatewayRequest& req, std::string& reason) const = 0;
    // 可扩展：enable/disable、优先级、动态参数等
};
