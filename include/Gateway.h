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
    // 无锁队列
    moodycamel::ConcurrentQueue<RequestEnvelope>& order_queue; // 下单、撤单队列，多生产者单消费者：多用户请求-撮合引擎
    // moodycamel::ConcurrentQueue<QueryRequest> query_queue; // 查单队列，多生产者单消费者：多用户请求-订单簿查询
    moodycamel::ConcurrentQueue<TradeResponse>& trade_response_queue; // 成交回报队列，单生产者单消费者：撮合引擎-网关
    moodycamel::ConcurrentQueue<MarketDataResponse>& market_data_queue; // 行情推送队列，单生产者单消费者：撮合引擎-网关
    
    // 订单状态映射表，维护订单状态，供查单请求查询
    std::unordered_map<uint64_t, OrderStatus> order_status_map; // order_id -> OrderStatus
    std::shared_mutex order_status_mutex; // 保护订单状态映射表的读写锁

    // 一个 uint64_t 是 64 位。毫秒级时间戳大约占 41 位（可以用到 2039 年），标的编号给 8 位（支持 256 个股票），递增序列号给 15 位（每毫秒支持 32768 笔下单）。
    // 拼接公式：tag = (timestamp << 23) | (symbol_id << 15) | (sequence_num & 0x7FFF)。
    // 维护一个原子变量作为递增序列号生成器
    std::atomic<uint64_t> sequence_num{0};
    std::pair<uint64_t, uint64_t> generate_ts_and_tag(uint32_t symbol_id) {
        uint64_t timestamp = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
        uint64_t tag = (timestamp << 23) | (symbol_id << 15) | (sequence_num.fetch_add(1) & 0x7FFF);
        return std::make_pair(timestamp, tag);
    }

    // 维护一个集合记录哪些交易员订阅了行情推送
    std::unordered_set<uint32_t> market_data_subscribers; // trader_id集合
    std::mutex market_data_subscribers_mutex; // 保护订阅者集合的互斥锁

    // 开关
    std::atomic<bool> running{false};
    // 处理下单、撤单的线程
    // std::thread order_processor;
    // 处理查单的线程
    // std::thread query_processor;
    // 分发成交回报的线程
    std::thread trade_response_procesor;
    // 分发行情推送的线程
    std::thread market_data_processor;
    // HTTP服务器线程
    std::thread http_server_thread;
    httplib::Server svr; 

    // void process_order_request() {
    //     RequestEnvelope req_env;
    //     while (running.load()) {
    //         if (order_queue.try_dequeue(req_env)) {
    //             // 放入订单队列
    //             order_queue.
    //             // 更新订单状态
    //             {
    //                 std::unique_lock<std::shared_mutex> lock(order_status_mutex);
    //                 order_status_map[req_env.data.order_req.tag] = OrderStatus::NEW;
    //             }
    //         } else {
    //             std::this_thread::yield();
    //         }
    //     }
    // }
    void process_query_request() {
        // 取出查单请求，从订单状态映射表查询订单状态，分发给用户。


    }
    void process_trade_responses() {
        // 取出成交回报，记录订单状态变化, 分发给用户。
        TradeResponse res;
        while (running.load()) {
            if (trade_response_queue.try_dequeue(res)) {
                {
                    std::unique_lock<std::shared_mutex> lock(order_status_mutex);
                    order_status_map[res.order_id] = res.status;
                }
                // 模拟“发给用户”：目前仅作为日志记录
                // 未来实现 WebSocket 时，这里将通过 trader_id 找到对应的 Session 并推送
                spdlog::info("Trade Response - order_id: {}, trader_id: {}, symbol_id: {}, side: {}, filled_qty: {}, price: {}, status: {}",
                    res.order_id, res.trader_id, res.symbol_id, res.side == Side::BUY ? "BUY" : "SELL", res.filled_qty, res.price, 
                    [&]{
                        switch (res.status) {
                            case OrderStatus::NEW: return "NEW";
                            case OrderStatus::PARTIAL_FILLED: return "PARTIAL_FILLED";
                            case OrderStatus::FILLED: return "FILLED";
                            case OrderStatus::EXPIRED: return "EXPIRED";
                            case OrderStatus::REJECTED: return "REJECTED";
                            case OrderStatus::CANCELED: return "CANCELED";
                            default: return "UNKNOWN";
                        }
                    }());
            } else {
                std::this_thread::yield();
            }
        }
    }
    void process_market_data() {

    }



public:
    Gateway(moodycamel::ConcurrentQueue<RequestEnvelope>& order_queue, 
            moodycamel::ConcurrentQueue<TradeResponse>& trade_response_queue,
            moodycamel::ConcurrentQueue<MarketDataResponse>& market_data_queue) : 
        order_queue(order_queue), trade_response_queue(trade_response_queue), market_data_queue(market_data_queue) {
        running.store(true);
        // order_processor = std::thread(&Gateway::process_order_request, this);
        // query_processor = std::thread(&Gateway::process_query_request, this);
        trade_response_procesor = std::thread(&Gateway::process_trade_responses, this);
        market_data_processor = std::thread(&Gateway::process_market_data, this);
        http_server_thread = std::thread(&Gateway::register_route, this);
    }

    ~Gateway() {
        running.store(false);
        svr.stop(); 
        // if (order_processor.joinable()) order_processor.join();
        // if (query_processor.joinable()) query_processor.join();
        if (trade_response_procesor.joinable()) trade_response_procesor.join();
        if (market_data_processor.joinable()) market_data_processor.join();
        if (http_server_thread.joinable()) http_server_thread.join();
    }

    // 路由注册: 下单、撤单、查单、行情订阅接口
    void register_route() {
        svr.set_address_family(AF_INET);
        svr.set_keep_alive_max_count(5);
        // 1.下单接口：/submit_order
        svr.Post("/submit_order", [this](const httplib::Request& req, httplib::Response& res) {
            try {
                auto body = nlohmann::json::parse(req.body);
                SubmitRequest submit_req;
                auto ts_and_tag = generate_ts_and_tag(body["symbol_id"]);
                submit_req.ts = ts_and_tag.first; // 怎么搞时间优先？只用tag到底够不够？
                submit_req.tag = ts_and_tag.second;
                submit_req.symbol_id = body["symbol_id"];
                submit_req.trader_id = body["trader_id"];
                submit_req.price = body["price"];
                submit_req.qty = body["qty"];
                submit_req.side = body["side"] == "BUY" ? Side::BUY : Side::SELL;
                submit_req.order_type = body["order_type"] == "LIMIT" ? OrderType::LIMIT : OrderType::MARKET;
                submit_req.tif = body["tif"] == "GTC" ? TimeInForce::GTC : (body["tif"] == "IOC" ? TimeInForce::IOC : TimeInForce::FOK);
                this->on_submit_order(submit_req); 
                nlohmann::json resp;
                resp["status"] = "ACCEPTED";
                resp["order_id"] = submit_req.tag;
                resp["timestamp"] = submit_req.ts;
                res.set_content(resp.dump(), "application/json");
            } catch (...) {
                res.status = 400;
                res.set_content("{\"status\":\"REJECTED\"}", "application/json");
            }
        });

        // 2.撤单接口：/cancel_order
        svr.Post("/cancel_order", [this](const httplib::Request& req, httplib::Response& res) {
            try {
                auto body = nlohmann::json::parse(req.body);
                CancelRequest cancel_req;
                cancel_req.order_id = body["order_id"];
                cancel_req.symbol_id = body["symbol_id"];
                cancel_req.trader_id = body["trader_id"];
                this->on_cancel_order(cancel_req); 
                res.set_content("{\"status\":\"ACCEPTED\"}", "application/json");
            } catch (...) {
                res.status = 400;
                res.set_content("{\"status\":\"REJECTED\"}", "application/json");
            }
        });

        // 既然网关用户查单 -> 网关查 map -> 异步放入已经用 unordered_map 缓存了最新状态，
        // HTTP 查单接口收到请求后，直接加读锁查 map，立刻返回 HTTP 响应即可。不需要再放进队列里绕一圈，那样反而增加了 HTTP 响应的延迟和复杂度。
        // 3.查单接口：/query_order
        svr.Post("/query_order", [this](const httplib::Request& req, httplib::Response& res) {
            try {
                auto body = nlohmann::json::parse(req.body);
                uint64_t oid = body["order_id"];
                // 使用读锁（Shared Lock），允许多个用户同时并发查单
                std::shared_lock<std::shared_mutex> lock(order_status_mutex);
                if (order_status_map.count(oid)) {
                    nlohmann::json resp;
                    resp["order_id"] = oid;
                    std::string status_str;
                    OrderStatus status = order_status_map[oid];
                    switch (status) {
                        case OrderStatus::NEW: status_str = "NEW"; break;
                        case OrderStatus::PARTIAL_FILLED: status_str = "PARTIAL_FILLED"; break;
                        case OrderStatus::FILLED: status_str = "FILLED"; break;
                        case OrderStatus::EXPIRED: status_str = "EXPIRED"; break;
                        case OrderStatus::REJECTED: status_str = "REJECTED"; break;
                        case OrderStatus::CANCELED: status_str = "CANCELED"; break;
                        default: status_str = "UNKNOWN"; break;
                    }
                    resp["status"] = status_str;
                    resp["message"] = "Success";
                    res.set_content(resp.dump(), "application/json");
                } else {
                    res.status = 404;
                    res.set_content("{\"status\":\"NOT_FOUND\", \"message\":\"Order ID does not exist\"}", "application/json");
                }
            } catch (...) {
                res.status = 400;
                res.set_content("{\"status\":\"REJECTED\"}", "application/json");
            }
        });

        // 4.行情订阅接口：/subscribe_market_data
        svr.Post("/subscribe_market_data", [this](const httplib::Request& req, httplib::Response& res) {
            try {
                auto body = nlohmann::json::parse(req.body);
                SubscribeMarketDataRequest subscribe_req;
                subscribe_req.symbol_id = body["symbol_id"];
                subscribe_req.trader_id = body["trader_id"];
                this->on_subscribe_market_data(subscribe_req);
                res.set_content("{\"status\":\"ACCEPTED\"}", "application/json");
            } catch (...) {
                res.status = 400;
                res.set_content("{\"status\":\"REJECTED\"}", "application/json");
            }

        });

        if (!svr.listen("127.0.0.1", 8080)) {
            spdlog::error("Failed to start HTTP server on ip = {}, port = {}", "127.0.0.1", 8080);
        } else {
            spdlog::info("HTTP server started on ip = {}, port = {}", "127.0.0.1", 8080);
        }
    }

    // 下单接口
    void on_submit_order(const SubmitRequest& req) {
        spdlog::debug("Received submit order request: symbol_id={}, trader_id={}, price={}, qty={}, side={}, order_type={}, tif={}",
            req.symbol_id, req.trader_id, req.price, req.qty, 
            req.side == Side::BUY ? "BUY" : "SELL", 
            req.order_type == OrderType::LIMIT ? "LIMIT" : "MARKET", 
            req.tif == TimeInForce::GTC ? "GTC" : (req.tif == TimeInForce::IOC ? "IOC" : "FOK"));
        // 打标签：标签 = 时间戳（timestamp） + 标的编号（symbol_id） + 递增序列号
        RequestEnvelope envelope;
        envelope.req_type = RequestType::ORDER;
        envelope.data.order_req = req;
        envelope.data.order_req.tag = req.tag; // 打标签
        order_queue.enqueue(envelope); // 加入下单队列
    }
    // 撤单接口
    void on_cancel_order(const CancelRequest& req) {
        spdlog::debug("Received cancel order request: order_id={}, symbol_id={}, trader_id={}",
            req.order_id, req.symbol_id, req.trader_id);
        RequestEnvelope envelope;
        envelope.req_type = RequestType::CANCEL;
        envelope.data.cancel_req = req;
        order_queue.enqueue(envelope); // 加入撤单队列
    }
    // 查单接口
    // void on_query_order(const QueryRequest& req) {
    //     spdlog::debug("Received query order request: order_id={}, symbol_id={}, trader_id={}",
    //         req.order_id, req.symbol_id, req.trader_id);
    //     // query_queue.enqueue(req); // 加入查单队列
    // }
    // 行情订阅
    void on_subscribe_market_data(const SubscribeMarketDataRequest& req) {
        spdlog::debug("Received subscribe market data request: symbol_id={}, trader_id={}",
            req.symbol_id, req.trader_id);
        // 这里有竞争锁的风险，会影响网关性能
        {
            std::lock_guard<std::mutex> lock(market_data_subscribers_mutex);
            market_data_subscribers.insert(req.trader_id); // 记录订阅者
        }
    }

    void mock_submit_and_cancel() {
        // 模拟提交一个订单
        SubmitRequest submit_req;
        auto ts_and_tag = generate_ts_and_tag(888);
        submit_req.ts = ts_and_tag.first;
        submit_req.tag = ts_and_tag.second;
        submit_req.symbol_id = 888;
        submit_req.trader_id = 1001;
        submit_req.price = 10050;
        submit_req.qty = 200;
        submit_req.side = Side::BUY;
        submit_req.order_type = OrderType::LIMIT;
        submit_req.tif = TimeInForce::GTC;
        this->on_submit_order(submit_req);

        // 提交一个订单把上面的订单吃掉
        SubmitRequest submit_req2;
        auto ts_and_tag2 = generate_ts_and_tag(888);
        submit_req2.ts = ts_and_tag2.first;
        submit_req2.tag = ts_and_tag2.second;
        submit_req2.symbol_id = 888;
        submit_req2.trader_id = 1002;
        submit_req2.price = 10040;
        submit_req2.qty = 300;
        submit_req2.side = Side::SELL;
        submit_req2.order_type = OrderType::LIMIT;
        submit_req2.tif = TimeInForce::GTC;
        this->on_submit_order(submit_req2);

        // 模拟撤单
        CancelRequest cancel_req;
        cancel_req.order_id = submit_req.tag; // 用订单ID撤单
        cancel_req.symbol_id = 888;
        cancel_req.trader_id = 1001;
        this->on_cancel_order(cancel_req);

        CancelRequest cancel_req2;
        cancel_req2.order_id = submit_req2.tag; // 用订单ID撤单
        cancel_req2.symbol_id = 888;
        cancel_req2.trader_id = 1002;
        this->on_cancel_order(cancel_req2);
    }

    // 压测入口：并发提交/撤单，验证撮合引擎在高压下的稳定性
    // thread_count: 并发压测线程数
    // ops_per_thread: 每个线程执行的下单操作次数（期间会穿插撤单）
    void stress_submit_cancel(int thread_count, int ops_per_thread) {
        if (thread_count <= 0 || ops_per_thread <= 0) {
            spdlog::warn("stress_submit_cancel skipped: invalid args thread_count={}, ops_per_thread={}",
                thread_count, ops_per_thread);
            return;
        }

        // 记录压测总耗时，用于评估吞吐能力
        auto stress_start = std::chrono::steady_clock::now();
        std::vector<std::thread> workers;
        workers.reserve(static_cast<size_t>(thread_count));

        // 原子计数器：统计总提交/撤单次数，避免多线程写冲突
        std::atomic<uint64_t> total_submits{0};
        std::atomic<uint64_t> total_cancels{0};

        // 启动 N 个工作线程，每个线程独立构造订单并通过网关接口入队
        for (int tid = 0; tid < thread_count; ++tid) {
            workers.emplace_back([this, tid, ops_per_thread, &total_submits, &total_cancels]() {
                // 每个线程独立随机源，减少同构请求导致的测试偏差
                std::mt19937_64 rng(static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()) + static_cast<uint64_t>(tid));
                std::uniform_int_distribution<int> side_dist(0, 1);
                std::uniform_int_distribution<int> price_dist(10000, 10100);
                std::uniform_int_distribution<int> qty_dist(1, 1000);
                std::uniform_int_distribution<int> coin(0, 99);
                std::uniform_int_distribution<uint32_t> symbol_dist(1, 3);

                // 保存本线程提交的订单ID，后续随机撤单与收尾撤单都会用到
                std::vector<uint64_t> local_order_ids;
                local_order_ids.reserve(static_cast<size_t>(ops_per_thread));

                for (int i = 0; i < ops_per_thread; ++i) {
                    SubmitRequest submit_req;
                    uint32_t symbol_id = symbol_dist(rng);
                    auto ts_and_tag = generate_ts_and_tag(symbol_id);
                    submit_req.ts = ts_and_tag.first;
                    submit_req.tag = ts_and_tag.second;
                    submit_req.symbol_id = symbol_id;
                    submit_req.trader_id = static_cast<uint32_t>(1000 + tid);
                    submit_req.price = price_dist(rng);
                    submit_req.qty = static_cast<uint32_t>(qty_dist(rng));
                    submit_req.side = side_dist(rng) == 0 ? Side::BUY : Side::SELL;
                    submit_req.order_type = OrderType::LIMIT;
                    submit_req.tif = TimeInForce::GTC;

                    on_submit_order(submit_req);
                    total_submits.fetch_add(1, std::memory_order_relaxed);
                    local_order_ids.push_back(submit_req.tag);

                    // 约 30% 概率触发撤单，模拟真实流量里“下单后很快撤单”的行为
                    if (!local_order_ids.empty() && coin(rng) < 30) {
                        std::uniform_int_distribution<size_t> pick(0, local_order_ids.size() - 1);
                        size_t idx = pick(rng);

                        CancelRequest cancel_req;
                        cancel_req.order_id = local_order_ids[idx];
                        cancel_req.symbol_id = symbol_id;
                        cancel_req.trader_id = submit_req.trader_id;
                        on_cancel_order(cancel_req);
                        total_cancels.fetch_add(1, std::memory_order_relaxed);

                        local_order_ids[idx] = local_order_ids.back();
                        local_order_ids.pop_back();
                    }
                }

                // 收尾：把本线程仍未撤掉的订单尽量全部撤掉，增加状态机覆盖面
                for (uint64_t oid : local_order_ids) {
                    CancelRequest cancel_req;
                    cancel_req.order_id = oid;
                    cancel_req.symbol_id = 1;
                    cancel_req.trader_id = static_cast<uint32_t>(1000 + tid);
                    on_cancel_order(cancel_req);
                    total_cancels.fetch_add(1, std::memory_order_relaxed);
                }
            });
        }

        // 等待所有压测线程结束，确保统计完整
        for (auto& w : workers) {
            if (w.joinable()) {
                w.join();
            }
        }

        auto stress_end = std::chrono::steady_clock::now();
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(stress_end - stress_start).count();
        // 输出压测摘要：线程数、总请求量与耗时
        spdlog::warn("Stress finished: threads={}, ops_per_thread={}, submits={}, cancels={}, elapsed_ms={}",
            thread_count,
            ops_per_thread,
            total_submits.load(std::memory_order_relaxed),
            total_cancels.load(std::memory_order_relaxed),
            elapsed_ms);
    }

    // HTTP 链路压测：网关解析 -> 请求入队 -> 撮合引擎处理 -> 成交回报队列消费
    void stress_http_pipeline(int thread_count, int ops_per_thread) {
        if (thread_count <= 0 || ops_per_thread <= 0) {
            spdlog::warn("stress_http_pipeline skipped: invalid args thread_count={}, ops_per_thread={}",
                thread_count, ops_per_thread);
            return;
        }

        auto start = std::chrono::steady_clock::now();
        std::vector<std::thread> workers;
        workers.reserve(static_cast<size_t>(thread_count));

        std::atomic<uint64_t> submit_ok{0};
        std::atomic<uint64_t> cancel_ok{0};
        std::atomic<uint64_t> query_ok{0};
        std::atomic<uint64_t> http_errors{0};

        for (int tid = 0; tid < thread_count; ++tid) {
            workers.emplace_back([this, tid, ops_per_thread, &submit_ok, &cancel_ok, &query_ok, &http_errors]() {
                httplib::Client cli("127.0.0.1", 8080);
                cli.set_keep_alive(true);

                std::mt19937_64 rng(static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()) + static_cast<uint64_t>(tid));
                std::uniform_int_distribution<int> side_dist(0, 1);
                std::uniform_int_distribution<int> price_dist(10000, 10100);
                std::uniform_int_distribution<int> qty_dist(1, 1000);
                std::uniform_int_distribution<int> symbol_dist(1, 3);
                std::uniform_int_distribution<int> coin_dist(0, 99);

                std::vector<uint64_t> order_ids;
                order_ids.reserve(static_cast<size_t>(ops_per_thread));

                for (int i = 0; i < ops_per_thread; ++i) {
                    uint32_t symbol_id = static_cast<uint32_t>(symbol_dist(rng));
                    uint32_t trader_id = static_cast<uint32_t>(10000 + tid);

                    nlohmann::json submit_payload;
                    submit_payload["symbol_id"] = symbol_id;
                    submit_payload["trader_id"] = trader_id;
                    submit_payload["price"] = price_dist(rng);
                    submit_payload["qty"] = qty_dist(rng);
                    submit_payload["side"] = side_dist(rng) == 0 ? "BUY" : "SELL";
                    submit_payload["order_type"] = "LIMIT";
                    submit_payload["tif"] = "GTC";

                    auto submit_res = cli.Post("/submit_order", submit_payload.dump(), "application/json");
                    if (submit_res && submit_res->status == 200) {
                        submit_ok.fetch_add(1, std::memory_order_relaxed);
                        try {
                            auto submit_json = nlohmann::json::parse(submit_res->body);
                            if (submit_json.contains("order_id")) {
                                order_ids.push_back(submit_json["order_id"].get<uint64_t>());
                            }
                        } catch (...) {
                            http_errors.fetch_add(1, std::memory_order_relaxed);
                        }
                    } else {
                        http_errors.fetch_add(1, std::memory_order_relaxed);
                        continue;
                    }

                    // 混入 query：施压网关解析和状态查询路径
                    if (!order_ids.empty() && coin_dist(rng) < 50) {
                        uint64_t oid = order_ids.back();
                        nlohmann::json query_payload;
                        query_payload["order_id"] = oid;
                        query_payload["symbol_id"] = symbol_id;
                        query_payload["trader_id"] = trader_id;

                        auto query_res = cli.Post("/query_order", query_payload.dump(), "application/json");
                        if (query_res && (query_res->status == 200 || query_res->status == 404)) {
                            query_ok.fetch_add(1, std::memory_order_relaxed);
                        } else {
                            http_errors.fetch_add(1, std::memory_order_relaxed);
                        }
                    }

                    // 混入 cancel：施压网关解析 -> 取消请求入队 -> 引擎处理路径
                    if (!order_ids.empty() && coin_dist(rng) < 30) {
                        uint64_t oid = order_ids.back();
                        nlohmann::json cancel_payload;
                        cancel_payload["order_id"] = oid;
                        cancel_payload["symbol_id"] = symbol_id;
                        cancel_payload["trader_id"] = trader_id;

                        auto cancel_res = cli.Post("/cancel_order", cancel_payload.dump(), "application/json");
                        if (cancel_res && cancel_res->status == 200) {
                            cancel_ok.fetch_add(1, std::memory_order_relaxed);
                            order_ids.pop_back();
                        } else {
                            http_errors.fetch_add(1, std::memory_order_relaxed);
                        }
                    }
                }
            });
        }

        for (auto& w : workers) {
            if (w.joinable()) {
                w.join();
            }
        }

        auto end = std::chrono::steady_clock::now();
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        double elapsed_s = static_cast<double>(elapsed_ms) / 1000.0;
        uint64_t total_ok = submit_ok.load(std::memory_order_relaxed) + cancel_ok.load(std::memory_order_relaxed) + query_ok.load(std::memory_order_relaxed);
        double total_qps = elapsed_s > 0.0 ? static_cast<double>(total_ok) / elapsed_s : 0.0;

        spdlog::warn("HTTP stress finished: threads={}, ops_per_thread={}, submit_ok={}, cancel_ok={}, query_ok={}, http_errors={}, elapsed_ms={}, total_qps={}",
            thread_count,
            ops_per_thread,
            submit_ok.load(std::memory_order_relaxed),
            cancel_ok.load(std::memory_order_relaxed),
            query_ok.load(std::memory_order_relaxed),
            http_errors.load(std::memory_order_relaxed),
            elapsed_ms,
            total_qps);
    }

};