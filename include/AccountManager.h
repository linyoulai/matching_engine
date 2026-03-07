#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <functional>

// 账户类型
enum class AccountType {
    INDIVIDUAL,
    INSTITUTION,
    BROKER,
    MARKET_MAKER
};

// 账户状态
enum class AccountStatus {
    ACTIVE,
    FROZEN,
    CLOSED,
    SUSPENDED
};

// 资金变动类型
enum class BalanceChangeType {
    DEPOSIT,
    WITHDRAW,
    FREEZE,
    UNFREEZE,
    TRADE_SETTLEMENT
};

// 账户资金流水
struct BalanceChangeRecord {
    uint64_t record_id;
    uint64_t account_id;
    BalanceChangeType type;
    double amount;
    double balance_after;
    std::string remark;
    uint64_t timestamp;
};

// 持仓信息
struct Position {
    std::string symbol;
    int quantity;
    double avg_price;
    // 可扩展：持仓方向、冻结数量等
};

// 账户信息
struct Account {
    uint64_t account_id;
    std::string owner_name;
    AccountType type;
    AccountStatus status;
    double available_balance;
    double frozen_balance;
    std::vector<Position> positions;
    // 可扩展：风险等级、权限、联系方式等
};

// 账户管理器
class AccountManager {
public:
    explicit AccountManager();
    // 新增账户
    void addAccount(const Account& account);
    // 查询账户
    Account getAccount(uint64_t account_id) const;
    // 修改账户状态
    void setAccountStatus(uint64_t account_id, AccountStatus status);
    // 资金变动
    void changeBalance(uint64_t account_id, double amount, BalanceChangeType type, const std::string& remark = "");
    // 冻结/解冻资金
    void freezeBalance(uint64_t account_id, double amount);
    void unfreezeBalance(uint64_t account_id, double amount);
    // 持仓变动
    void updatePosition(uint64_t account_id, const std::string& symbol, int delta_qty, double price);
    // 查询持仓
    std::vector<Position> getPositions(uint64_t account_id) const;
    // 资金流水查询
    std::vector<BalanceChangeRecord> getBalanceHistory(uint64_t account_id) const;
    // 权限校验
    void setPermissionCallback(const std::function<bool(uint64_t, const std::string&)>& cb);
    // 日志回调
    void setLogCallback(const std::function<void(const std::string&)>& cb);
    void log(const std::string& msg) const;
private:
    std::unordered_map<uint64_t, Account> accounts_;
    std::unordered_map<uint64_t, std::vector<BalanceChangeRecord>> balance_history_;
    std::function<bool(uint64_t, const std::string&)> permission_cb_;
    // ...
};
