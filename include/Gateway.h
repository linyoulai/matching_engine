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
#include "../lib/concurrentqueue.h"
#include "Request.h"
#include "Response.h"
#include "../lib/httplib.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>


// 网关
class Gateway {
private:
    // 4个无锁队列
    moodycamel::ConcurrentQueue<RequestEnvelope> order_queue; // 下单、撤单队列，多生产者单消费者：多用户请求-撮合引擎
    moodycamel::ConcurrentQueue<QueryRequest> query_queue; // 查单队列，多生产者单消费者：多用户请求-订单簿查询
    moodycamel::ConcurrentQueue<TradeResponse> trade_response_queue; // 成交回报队列，单生产者单消费者：撮合引擎-网关
    moodycamel::ConcurrentQueue<MarketDataResponse> market_data_queue; // 行情推送队列，单生产者单消费者：撮合引擎-网关
    
    // 订单状态映射表，维护订单状态，供查单请求查询
    std::unordered_map<uint64_t, OrderStatus> order_status_map; // order_id -> OrderStatus
    std::shared_mutex order_status_mutex; // 保护订单状态映射表的读写锁

    // 维护一个原子变量作为递增序列号生成器
    std::atomic<uint64_t> sequence_num{0};
    std::pair<uint64_t, uint64_t> generate_ts_and_tag(uint32_t symbol_id) {
        uint64_t timestamp = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
        uint64_t tag = (timestamp << 32) | (symbol_id << 16) | (sequence_num.fetch_add(1) & 0xFFFF);
        return std::make_pair(timestamp, tag);
    }

    // 维护一个集合记录哪些交易员订阅了行情推送
    std::unordered_set<uint32_t> market_data_subscribers; // trader_id集合
    std::mutex market_data_subscribers_mutex; // 保护订阅者集合的互斥锁

    // 开关
    std::atomic<bool> running{false};
    // 处理下单、撤单的线程
    std::thread order_processor;
    // 处理查单的线程
    std::thread query_processor;
    // 分发成交回报的线程
    std::thread trade_response_procesor;
    // 分发行情推送的线程
    std::thread market_data_processor;
    // HTTP服务器线程
    std::thread http_server_thread;
    httplib::Server svr; 

    void process_order_request() {

    }
    void process_query_request() {
        
    }
    void process_trade_responses() {

    }
    void process_market_data() {

    }



public:
    Gateway() {
        running.store(true);
        order_processor = std::thread(&Gateway::process_order_request, this);
        query_processor = std::thread(&Gateway::process_query_request, this);
        trade_response_procesor = std::thread(&Gateway::process_trade_responses, this);
        market_data_processor = std::thread(&Gateway::process_market_data, this);
        http_server_thread = std::thread(&Gateway::register_route, this);
    }

    ~Gateway() {
        running.store(false);
        svr.stop(); 
        if (order_processor.joinable()) order_processor.join();
        if (query_processor.joinable()) query_processor.join();
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
                res.set_content("{\"status\":\"ACCEPTED\"}", "application/json");
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

        // 3.查单接口：/query_order
        svr.Post("/query_order", [this](const httplib::Request& req, httplib::Response& res) {
            try {
                auto body = nlohmann::json::parse(req.body);
                QueryRequest query_req;
                query_req.order_id = body["order_id"];
                query_req.symbol_id = body["symbol_id"];
                query_req.trader_id = body["trader_id"];
                this->on_query_order(query_req); 
                res.set_content("{\"status\":\"ACCEPTED\"}", "application/json");
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
    void on_query_order(const QueryRequest& req) {
        spdlog::debug("Received query order request: order_id={}, symbol_id={}, trader_id={}",
            req.order_id, req.symbol_id, req.trader_id);
        query_queue.enqueue(req); // 加入查单队列
    }
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

};