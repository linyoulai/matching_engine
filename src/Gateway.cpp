#include "Gateway.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

Gateway::Gateway(moodycamel::ConcurrentQueue<RequestEnvelope>& order_queue, 
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

Gateway::~Gateway() {
    running.store(false);
    svr.stop(); 
    // if (order_processor.joinable()) order_processor.join();
    // if (query_processor.joinable()) query_processor.join();
    if (trade_response_procesor.joinable()) trade_response_procesor.join();
    if (market_data_processor.joinable()) market_data_processor.join();
    if (http_server_thread.joinable()) http_server_thread.join();
}

uint64_t Gateway::generate_tag() {
    return sequence_num.fetch_add(1);
}

void Gateway::process_trade_responses() {
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
            spdlog::info("交易响应 - order_id: {}, trader_id: {}, symbol_id: {}, side: {}, filled_qty: {}, price: {}, status: {}",
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

void Gateway::process_market_data() {

}

// 路由注册: 下单、撤单、查单、行情订阅接口
void Gateway::register_route() {
    svr.set_address_family(AF_INET);
    svr.set_keep_alive_max_count(5);
    // 1.下单接口：/submit_order
    svr.Post("/submit_order", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = nlohmann::json::parse(req.body);
            SubmitRequest submit_req;
            uint64_t tag = generate_tag();
            submit_req.ts = tag; // 怎么搞时间优先？只用tag到底够不够？
            submit_req.tag = tag;
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

    // 依然使用读锁（Shared Lock），允许多个用户同时并发查单
    // 3.查单接口：/query_order
    svr.Post("/query_order", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = nlohmann::json::parse(req.body);
            uint64_t oid = body["order_id"];
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
        spdlog::error("启动 HTTP 服务失败，配置为 ip = {}, port = {}", "127.0.0.1", 8080);
    } else {
        spdlog::info("HTTP 服务已启动，监听 ip = {}, port = {}", "127.0.0.1", 8080);
    }
}

// 下单接口
void Gateway::on_submit_order(const SubmitRequest& req) {
    spdlog::debug("收到提交订单请求: symbol_id={}, trader_id={}, price={}, qty={}, side={}, order_type={}, tif={}",
        req.symbol_id, req.trader_id, req.price, req.qty, 
        req.side == Side::BUY ? "BUY" : "SELL", 
        req.order_type == OrderType::LIMIT ? "LIMIT" : "MARKET", 
        req.tif == TimeInForce::GTC ? "GTC" : (req.tif == TimeInForce::IOC ? "IOC" : "FOK"));
    RequestEnvelope envelope;
    envelope.req_type = RequestType::ORDER;
    envelope.data.order_req = req;
    envelope.data.order_req.tag = req.tag; // 打标签
    order_queue.enqueue(envelope); // 加入下单队列
}

// 撤单接口
void Gateway::on_cancel_order(const CancelRequest& req) {
    spdlog::debug("收到撤销订单请求: order_id={}, symbol_id={}, trader_id={}",
        req.order_id, req.symbol_id, req.trader_id);
    RequestEnvelope envelope;
    envelope.req_type = RequestType::CANCEL;
    envelope.data.cancel_req = req;
    order_queue.enqueue(envelope); // 加入撤单队列
}

// 行情订阅
void Gateway::on_subscribe_market_data(const SubscribeMarketDataRequest& req) {
    spdlog::debug("收到订阅市场数据请求: symbol_id={}, trader_id={}",
        req.symbol_id, req.trader_id);
    std::lock_guard<std::mutex> lock(market_data_subscribers_mutex);
    market_data_subscribers.insert(req.trader_id); // 记录订阅者
}

void Gateway::mock_submit_and_cancel() {
    SubmitRequest submit_req;
    uint64_t tag = generate_tag();
    submit_req.ts = tag;
    submit_req.tag = tag;
    submit_req.symbol_id = 888;
    submit_req.trader_id = 1001;
    submit_req.price = 10050;
    submit_req.qty = 200;
    submit_req.side = Side::BUY;
    submit_req.order_type = OrderType::LIMIT;
    submit_req.tif = TimeInForce::GTC;
    this->on_submit_order(submit_req);

    SubmitRequest submit_req2;
    uint64_t tag2 = generate_tag();
    submit_req2.ts = tag2;
    submit_req2.tag = tag2;
    submit_req2.symbol_id = 888;
    submit_req2.trader_id = 1002;
    submit_req2.price = 10040;
    submit_req2.qty = 300;
    submit_req2.side = Side::SELL;
    submit_req2.order_type = OrderType::LIMIT;
    submit_req2.tif = TimeInForce::GTC;
    this->on_submit_order(submit_req2);

    CancelRequest cancel_req;
    cancel_req.order_id = submit_req.tag;
    cancel_req.symbol_id = 888;
    cancel_req.trader_id = 1001;
    this->on_cancel_order(cancel_req);

    CancelRequest cancel_req2;
    cancel_req2.order_id = submit_req2.tag;
    cancel_req2.symbol_id = 888;
    cancel_req2.trader_id = 1002;
    this->on_cancel_order(cancel_req2);
}

void Gateway::stress_submit_cancel(int thread_count, int ops_per_thread) {
    if (thread_count <= 0 || ops_per_thread <= 0) {
        spdlog::warn("跳过 stress_submit_cancel: 参数无效 thread_count={}, ops_per_thread={}",
            thread_count, ops_per_thread);
        return;
    }

    auto stress_start = std::chrono::steady_clock::now();
    std::vector<std::thread> workers;
    workers.reserve(static_cast<size_t>(thread_count));

    std::atomic<uint64_t> total_submits{0};
    std::atomic<uint64_t> total_cancels{0};

    for (int tid = 0; tid < thread_count; ++tid) {
        workers.emplace_back([this, tid, ops_per_thread, &total_submits, &total_cancels]() {
            std::mt19937_64 rng(static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()) + static_cast<uint64_t>(tid));
            std::uniform_int_distribution<int> side_dist(0, 1);
            std::uniform_int_distribution<int> price_dist(10000, 10100);
            std::uniform_int_distribution<int> qty_dist(1, 1000);
            std::uniform_int_distribution<int> coin(0, 99);
            std::uniform_int_distribution<uint32_t> symbol_dist(1, 3);

            std::vector<uint64_t> local_order_ids;
            local_order_ids.reserve(static_cast<size_t>(ops_per_thread));

            for (int i = 0; i < ops_per_thread; ++i) {
                SubmitRequest submit_req;
                uint32_t symbol_id = symbol_dist(rng);
                uint64_t tag = generate_tag();
                submit_req.ts = tag;
                submit_req.tag = tag;
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

    for (auto& w : workers) {
        if (w.joinable()) {
            w.join();
        }
    }

    auto stress_end = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(stress_end - stress_start).count();
    spdlog::warn("压力测试完成: threads={}, ops_per_thread={}, submits={}, cancels={}, elapsed_ms={}",
        thread_count, ops_per_thread, total_submits.load(std::memory_order_relaxed), total_cancels.load(std::memory_order_relaxed), elapsed_ms);
}

void Gateway::stress_http_pipeline(int thread_count, int ops_per_thread) {
    if (thread_count <= 0 || ops_per_thread <= 0) {
        spdlog::warn("跳过 stress_http_pipeline: 参数无效 thread_count={}, ops_per_thread={}",
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

    spdlog::warn("HTTP 压力测试完成: threads={}, ops_per_thread={}, submit_ok={}, cancel_ok={}, query_ok={}, http_errors={}, elapsed_ms={}, total_qps={}",
        thread_count, ops_per_thread, submit_ok.load(std::memory_order_relaxed), cancel_ok.load(std::memory_order_relaxed),
        query_ok.load(std::memory_order_relaxed), http_errors.load(std::memory_order_relaxed), elapsed_ms, total_qps);
}
