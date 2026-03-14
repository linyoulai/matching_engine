// include/Gateway.h
/*
3.网关（Gateway）：开放HTTP接口。总共4个无锁队列，使用moodycamel::ConcurrentQueue。
    开放接口：
        HTTP接口：从接口中提取数据，打标签，放入队列
            /submit_order
                示例：
                    请求字段：
                    {
                        "symbol_id": 888,
                        "trader_id": 1001,
                        "price": 10050, 
                        "qty": 200,
                        "side": "BUY",
                        "order_type": "LIMIT",
                        "tif": "GTC"
                    }
                    响应：
                    {
                        "status": "ACCEPTED",
                        "order_id": 1741416550000001,
                        "message": "Order has been queued."
                    }
            /cancel_order
                示例：
                    {
                        "order_id": 1741416550000001,
                        "symbol_id": 888,
                        "trader_id": 1001
                    }
                    {
                        "status": "ACCEPTED",
                        "order_id": 1741416550000001,
                        "message": "Cancellation request queued."
                    }
                    {
                        "status": "SUCCESS",
                        "order_id": 1741416550000001,
                        "message": "canceled sucessfully"
                    }
                    {
                        "status": "Failed",
                        "order_id": 1741416550000001,
                        "message": "cancel failed"
                    }
            /query_order
                示例：
                    {
                        "order_id": 1741416550000001,
                        "symbol_id": 888,
                        "trader_id": 1001
                    }
                    {
                        "order_id": 1741416550000001,
                        "status": "PARTIAL_FILLED",
                        "filled_qty": 100,
                        "remaining_qty": 100,
                        "avg_price": 10050
                    }
        Websocket接口：
            暂时不实现，以后再写。
    响应：
        HTTP即时响应：
        {
            "status": "ACCEPTED",
            "order_id": 1741416550000001, 
            "timestamp": 1741416550
        }
        成交回报：
        {
            "response_type": "TRADE_RESPONSE",
            "order_id": 1741416550000001,
            "trader_id": ,
            "symbol_id": ,
            "side": ,
            "filled_qty": 100,
            "price": 10050,
            "status": "FILLED"
        }
        行情推送(5档):
        {
            "response_type": "MARKET_DATA_SNAPSHOT",
            "symbol_id": 888,
            "timestamp": 1741416550,
            "bids": [
                {"price": 10049, "qty": 500},
                {"price": 10048, "qty": 300},
                {"price": 10047, "qty": 1200},
                {"price": 10046, "qty": 800},
                {"price": 10045, "qty": 100}
            ],
            "asks": [
                {"price": 10051, "qty": 400},
                {"price": 10052, "qty": 600},
                {"price": 10053, "qty": 200},
                {"price": 10054, "qty": 900},
                {"price": 10055, "qty": 150}
            ]
        }
    输入队列：
        下单、撤单OrderQueue：
            打标签：负责给用户请求打标签（tag），标签 = 时间戳（timestamp） + 标的编号（symbol_id） + 递增序列号（递增序列号怎么获取？原子变量）
            定序：然后异步地把请求(SubmitRequest、CancelRequest)加入输入无锁队列（RequestQueue）。
        查单OrderStatusQueue请求(QueryRequest)：
            查单(query_order)：在网关模块维护一个std::unordered_map(order_id, OrderStatus)，map监听TradeResponseQueue来实时更新，用户查单时从这里获取订单状态，然后异步地放入订单状态无锁队列（OrderStatusQueue），由另一个线程去进行分发。
    输出队列：
        成交回报队列(TradeResponseQueue(TradeResponse))：创建一个消费者线程专门分发成交回报。
            成交回报应包含字段：response_type, order_id, trader_id, symbol_id, side, filled_qty, price, status
        行情推送队列(MarketDataQueue(MarketDataResponse))：创建一个消费者线程专门分发行情推送。
            行情推送应包含字段：response_type, symbol_id, timestamp, bids(price, qty), asks(price, qty).
*/
#pragma once
#include <cstdint>
#include <unordered_map>
#include <shared_mutex> // C++17
#include <thread>
#include <unordered_set>
#include <vector>
#include <random>
#include <chrono>
#include <algorithm>
#include "../lib/concurrentqueue.h"
#include "Request.h"
#include "Response.h"
#include "../lib/httplib.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>


// 网关
class Gateway {
private:
    moodycamel::ConcurrentQueue<RequestEnvelope>& order_queue; // 下单、撤单队列，多生产者单消费者：多用户请求-撮合引擎
    moodycamel::ConcurrentQueue<TradeResponse>& trade_response_queue; // 成交回报队列，单生产者单消费者：撮合引擎-网关
    moodycamel::ConcurrentQueue<MarketDataResponse>& market_data_queue; // 行情推送队列，单生产者单消费者：撮合引擎-网关
    // 订单状态映射表，维护订单状态，供查单请求查询
    std::unordered_map<uint64_t, OrderStatus> order_status_map; // order_id -> OrderStatus
    std::shared_mutex order_status_mutex; // 保护订单状态映射表的读写锁
    // 维护一个原子变量作为递增序列号生成器tag = sequence_num
    std::atomic<uint64_t> sequence_num{1};
    uint64_t generate_tag();

    // 维护一个集合记录哪些交易员订阅了行情推送
    std::unordered_set<uint32_t> market_data_subscribers; // trader_id集合
    std::mutex market_data_subscribers_mutex; // 保护订阅者集合的互斥锁

    // 开关
    std::atomic<bool> running{false};
    // 分发成交回报的线程
    std::thread trade_response_procesor;
    // 分发行情推送的线程
    std::thread market_data_processor;
    // HTTP服务器线程
    std::thread http_server_thread;
    httplib::Server svr; 

    void process_trade_responses();
    void process_market_data();
    void register_route();

public:
    explicit Gateway(moodycamel::ConcurrentQueue<RequestEnvelope>& order_queue, 
                     moodycamel::ConcurrentQueue<TradeResponse>& trade_response_queue,
                     moodycamel::ConcurrentQueue<MarketDataResponse>& market_data_queue);

    ~Gateway();

    // 路由注册: 下单、撤单、查单、行情订阅接口

    // 下单接口
    void on_submit_order(const SubmitRequest& req);
    // 撤单接口
    void on_cancel_order(const CancelRequest& req);
    // 行情订阅
    void on_subscribe_market_data(const SubscribeMarketDataRequest& req);

    void mock_submit_and_cancel();

    // 压测入口：并发提交/撤单，验证撮合引擎在高压下的稳定性
    // thread_count: 并发压测线程数
    // ops_per_thread: 每个线程执行的下单操作次数（期间会穿插撤单）
    void stress_submit_cancel(int thread_count, int ops_per_thread);

    // HTTP 链路压测：网关解析 -> 请求入队 -> 撮合引擎处理 -> 成交回报队列消费
    void stress_http_pipeline(int thread_count, int ops_per_thread);

};