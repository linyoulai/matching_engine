#pragma once
#include "RiskRule.h"
#include <vector>
#include <memory>
#include <string>

// 风控管理器，统一管理所有风控规则
class RiskControlManager {
public:
    explicit RiskControlManager();
    // 添加风控规则
    void addRule(std::shared_ptr<RiskRule> rule);
    // 按名称移除规则
    void removeRule(const std::string& rule_name);
    // 更新规则（热更新）
    void updateRule(const std::string& rule_name, std::shared_ptr<RiskRule> new_rule);
    // 启用/禁用规则
    void enableRule(const std::string& rule_name, bool enable);
    // 设置规则优先级
    void setRulePriority(const std::string& rule_name, int priority);
    // 分组管理：添加规则到分组
    void addRuleToGroup(const std::string& group, std::shared_ptr<RiskRule> rule);
    // 分组管理：移除分组
    void removeGroup(const std::string& group);
    // 校验所有规则，reason为首个失败原因
    bool checkAll(const GatewayRequest& req, std::string& reason) const;
    // 校验指定分组规则
    bool checkGroup(const std::string& group, const GatewayRequest& req, std::string& reason) const;
    // 查询所有规则名
    std::vector<std::string> listRuleNames() const;
    // 查询所有分组名
    std::vector<std::string> listGroupNames() const;
    // 查询规则优先级
    int getRulePriority(const std::string& rule_name) const;
    // 日志接口
    void setLogCallback(const std::function<void(const std::string&)>& cb);
    void log(const std::string& msg) const;
private:
    std::vector<std::shared_ptr<RiskRule>> rules_;
    // 分组、优先级、状态等可用map等结构实现
    // ...
};
