#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <functional>

// 持久化数据类型
enum class PersistenceDataType {
    ORDER,
    TRADE,
    ACCOUNT,
    POSITION,
    MARKET_DATA,
    BALANCE_CHANGE,
    LOG,
    CUSTOM
};

// 持久化操作类型
enum class PersistenceOpType {
    INSERT,
    UPDATE,
    DELETE_,
    QUERY
};

// 持久化请求
struct PersistenceRequest {
    PersistenceDataType data_type;
    PersistenceOpType op_type;
    std::string key;           // 主键或唯一标识
    std::string payload;       // 序列化后的数据（如JSON、二进制等）
    uint64_t timestamp;
    // 可扩展：批量操作、事务ID等
};

// 持久化响应
struct PersistenceResponse {
    bool success;
    std::string message;
    std::string payload;       // 查询结果等
    uint64_t timestamp;
};

// 持久化服务接口
class PersistenceService {
public:
    explicit PersistenceService();
    // 持久化写入/更新/删除
    void write(const PersistenceRequest& req);
    // 持久化查询
    PersistenceResponse query(const PersistenceRequest& req);
    // 批量写入
    void writeBatch(const std::vector<PersistenceRequest>& reqs);
    // 事务支持
    void beginTransaction();
    void commitTransaction();
    void rollbackTransaction();
    // 持久化回调（如异步写入完成通知）
    void setCallback(const std::function<void(const PersistenceResponse&)>& cb);
    // 日志回调
    void setLogCallback(const std::function<void(const std::string&)>& cb);
    void log(const std::string& msg) const;
private:
    // ...可扩展具体存储实现、缓存、索引等
};
