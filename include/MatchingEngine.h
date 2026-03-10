// include/MatchingEngine.h
#pragma once
#include "../lib/concurrentqueue.h"
#include "Response.h"
#include "Request.h"
#include "Order.h"
#include "OrderBook.h"

/*
5.单线程撮合引擎（MatchingEngine）：
    这是核心，撮合引擎会从输入无锁队列中取出请求并尝试撮合，如果不能撮合就加入订单簿。
    应该支持下单（submit_order）、撤单(cancel_order)。
    撮合引擎每秒生成5档盘口快照（这里的5档是指有单的价格档位，如果所剩单不足5档，往后读到5档并置零即可），异步发送给行情推送无锁队列（MarketDataQueue）。
    风险控制：
        1.大额市价单保护：用户下单撮合只能吃5档订单，不能超出。
        2.防止价格超出：用户下单时检查价格是否大于涨停板或小于跌停板，如果是的话就拒绝（不知道这个规则是否合理？）
        3.防止自成交：用户下单后撮合时检查trader_id是否相同，如果能够撮合成功但是trader_id相同，立即停止撮合，但不撤回之前撮合成功的成交。剩余的订单状态改为REJECTED，并发送给TradeResponseQueue。
    核心成员：
        下单撤单队列的引用 order_queue 撮合引擎是消费者
        成交回报队列的引用 trade_response_queue 撮合引擎是生产者
        行情推送队列的引用 market_data_queue 撮合引擎是生产者
        订单簿 OrderBook 维护买卖双方的订单
    核心函数：
        构造函数: 传入引用order_queue, trade_response_queue, market_data_queue
        死循环运行 run() : 轮询 order_queue , 如果有订单则 try_match()。
        尝试撮合 try_match() ：如果撮合成功，生成成交回报，如果有剩余或者没能撮合成功，加入订单簿。
*/

class MatchingEngine {
private:
    std::thread order_processor;
    moodycamel::ConcurrentQueue<RequestEnvelope>& order_queue; // 下单、撤单队列，多生产者单消费者：多用户请求-撮合引擎
    moodycamel::ConcurrentQueue<TradeResponse>& trade_response_queue; // 成交回报队列，单生产者单消费者：撮合引擎-网关
    moodycamel::ConcurrentQueue<MarketDataResponse>& market_data_queue; // 行情推送队列，单生产者单消费者：撮合引擎-网关
    std::atomic<bool> running{false};

    OrderBook order_book;
public:
    explicit MatchingEngine(moodycamel::ConcurrentQueue<RequestEnvelope>& order_queue, 
            moodycamel::ConcurrentQueue<TradeResponse>& trade_response_queue,
            moodycamel::ConcurrentQueue<MarketDataResponse>& market_data_queue) : 
            order_queue(order_queue), trade_response_queue(trade_response_queue), market_data_queue(market_data_queue) {
        // 创建撮合引擎线程
        running.store(true);
        order_processor = std::thread(&MatchingEngine::run, this);
    }
    ~MatchingEngine() {
        // 停止撮合引擎线程
        running.store(false);
        if (order_processor.joinable()) {
            order_processor.join();
        }
    }

    void run() {
        RequestEnvelope req_env;
        while (running.load()) {
            if (order_queue.try_dequeue(req_env)) {
                if (req_env.req_type == RequestType::ORDER) {
                    // 处理下单请求
                    Order order;
                    order.order_id = req_env.data.order_req.tag; // 订单ID直接用标签
                    order.ts = req_env.data.order_req.ts;
                    order.symbol_id = req_env.data.order_req.symbol_id;
                    order.trader_id = req_env.data.order_req.trader_id;
                    order.price = req_env.data.order_req.price;
                    order.qty = req_env.data.order_req.qty;
                    order.filled_qty = 0;
                    order.side = req_env.data.order_req.side;
                    order.order_type = req_env.data.order_req.order_type;
                    order.tif = req_env.data.order_req.tif;
                    order.order_status = OrderStatus::NEW;
                    spdlog::debug("Received order request: order_id {}, trader_id {}, symbol_id {}, side {}, price {}, qty {}, order_type {}, tif {}",
                        order.order_id, order.trader_id, order.symbol_id, order.side == Side::BUY ? "BUY" : "SELL", 
                        order.price, order.qty, 
                        order.order_type == OrderType::LIMIT ? "LIMIT" : "MARKET", 
                        order.tif == TimeInForce::GTC ? "GTC" : (order.tif == TimeInForce::IOC ? "IOC" : "FOK"));
                    try_macth(order);
                } else if (req_env.req_type == RequestType::CANCEL) {
                    // 处理撤单请求
                    spdlog::debug("Received cancel request: order_id {}, trader_id {}, symbol_id {}",
                        req_env.data.cancel_req.order_id, req_env.data.cancel_req.trader_id, req_env.data.cancel_req.symbol_id);
                    try_cancel(req_env.data.cancel_req);
                } else if (req_env.req_type == RequestType::SUBSCRIBE_MARKET_DATA) {
                    // 处理行情订阅请求
                    // subscribe_market_data(req_env.data.subscribe_md_req);
                }
            } else {
                std::this_thread::yield(); // 没有请求时让出 CPU，避免忙等待
            }
        }
    }

    bool is_over_limits(const Order& order) {
        // 实现价格限制检查逻辑
        if (order.price < order_book.get_lower_limit_price() || order.price > order_book.get_upper_limit_price()) {
            return true;
        }
        return false;
    }

    bool can_fill_all(const Order& order) {
        // 实现检查是否能全额成交的逻辑
        if (order.side == Side::BUY) {
            int64_t best_ask = order_book.get_best_ask();
            uint32_t total_available_qty = 0;
            while (best_ask != -1 && order.price >= best_ask) {
                const std::list<Order>& ask_list = order_book.ask_book[order_book.price_to_index(best_ask)];
                for (const Order& ask_order : ask_list) {
                    total_available_qty += (ask_order.qty - ask_order.filled_qty);
                    if (total_available_qty >= order.qty) {
                        return true; // 足够的卖单可供成交
                    }
                }
                // 寻找下一个最优卖价
                while (order_book.ask_book[order_book.price_to_index(best_ask)].empty()) {
                    if (best_ask < order_book.get_upper_limit_price()) {
                        best_ask++;
                    } else {
                        best_ask = -1; // 没有更多卖单了
                        break;
                    }
                }
            }
        } else { // SELL
            int64_t best_bid = order_book.get_best_bid();
            uint32_t total_available_qty = 0;
            while (best_bid != -1 && order.price <= best_bid) {
                const std::list<Order>& bid_list = order_book.bid_book[order_book.price_to_index(best_bid)];
                for (const Order& bid_order : bid_list) {
                    total_available_qty += (bid_order.qty - bid_order.filled_qty);
                    if (total_available_qty >= order.qty) {
                        return true; // 足够的买单可供成交
                    }
                }
                // 寻找下一个最优买价
                while (order_book.bid_book[order_book.price_to_index(best_bid)].empty()) {
                    if (best_bid > order_book.get_lower_limit_price()) {
                        best_bid--;
                    } else {
                        best_bid = -1; // 没有更多买单了
                        break;
                    }
                }
            }
        }
        return false;
    } 


    void try_macth(Order& order) {
        // 不能超出张跌停板
        if (is_over_limits(order)) {
            // 生成一个撤单/拒绝回报
            TradeResponse reject_resp;
            reject_resp.order_id = order.order_id;
            reject_resp.trader_id = order.trader_id;
            reject_resp.symbol_id = order.symbol_id;
            reject_resp.side = order.side;
            reject_resp.price = order.price;
            reject_resp.filled_qty = 0;
            reject_resp.status = OrderStatus::REJECTED;
            trade_response_queue.enqueue(reject_resp);
            spdlog::info("Order {} rejected due to price {} being out of limits", order.order_id, order.price);
            return;
        }

        // 处理FOK
        if (order.tif == TimeInForce::FOK && !can_fill_all(order)) {
            // 生成一个撤单/拒绝回报
            TradeResponse reject_resp;
            reject_resp.order_id = order.order_id;
            reject_resp.trader_id = order.trader_id;
            reject_resp.symbol_id = order.symbol_id;
            reject_resp.side = order.side;
            reject_resp.price = order.price;
            reject_resp.filled_qty = 0;
            reject_resp.status = OrderStatus::REJECTED;
            trade_response_queue.enqueue(reject_resp);
            spdlog::info("FOK Order {} rejected due to insufficient liquidity", order.order_id);
            return;
        }

        if (order.side == Side::BUY) {
            if (order.order_type == OrderType::LIMIT) {
                if (order.tif == TimeInForce::GTC) {
                    int64_t best_ask = order_book.get_best_ask();
                    // 可以撮合，执行撮合逻辑
                    while (order.qty > order.filled_qty && best_ask != -1 && order.price >= best_ask) {
                        Order& best_ask_order = order_book.ask_book[order_book.price_to_index(best_ask)].front();
                        uint32_t trade_qty = std::min(order.qty - order.filled_qty, best_ask_order.qty - best_ask_order.filled_qty);
                        int64_t trade_price = best_ask_order.price; // 价格优先原则，成交价为对手方订单价格
                        order.filled_qty += trade_qty;
                        best_ask_order.filled_qty += trade_qty;

                        // 生成买方成交回报
                        TradeResponse trade_response;
                        trade_response.order_id = order.order_id;
                        trade_response.trader_id = order.trader_id;
                        trade_response.symbol_id = order.symbol_id;
                        trade_response.side = order.side;
                        trade_response.price = trade_price;
                        trade_response.filled_qty = trade_qty;
                        trade_response.status = (order.qty == order.filled_qty) ? OrderStatus::FILLED : OrderStatus::PARTIAL_FILLED;
                        trade_response_queue.enqueue(trade_response); // 发送

                        // 生成卖方成交回报
                        TradeResponse sell_trade_response;
                        sell_trade_response.order_id = best_ask_order.order_id;
                        sell_trade_response.trader_id = best_ask_order.trader_id;
                        sell_trade_response.symbol_id = best_ask_order.symbol_id;
                        sell_trade_response.side = best_ask_order.side;
                        sell_trade_response.price = trade_price;
                        sell_trade_response.filled_qty = trade_qty;
                        sell_trade_response.status = (best_ask_order.qty == best_ask_order.filled_qty) ? OrderStatus::FILLED : OrderStatus::PARTIAL_FILLED;
                        trade_response_queue.enqueue(sell_trade_response); // 发送

                        if (best_ask_order.qty == best_ask_order.filled_qty) {
                            // 对手方订单完全成交，移除订单簿中的订单
                            spdlog::debug("Best ask order_id {} fully filled, removing from order book", best_ask_order.order_id);
                            order_book.order_map.erase(best_ask_order.order_id);
                            order_book.ask_book[order_book.price_to_index(best_ask)].pop_front();
                        } 
                        if (order.filled_qty == order.qty) {
                            // 当前订单完全成交，退出循环
                            break;
                        }

                        // 寻找下一个最优卖价
                        while (order_book.ask_book[order_book.price_to_index(best_ask)].empty()) {
                            if (best_ask < order_book.get_upper_limit_price()) {
                                best_ask++;
                            } else {
                                best_ask = -1; // 没有更多卖单了
                                break;
                            }
                        }
                    }
                    if (order.qty > order.filled_qty) {
                        // 还有剩余，加入订单簿
                        order_book.add_order(order);
                    }
                } else if (order.tif == TimeInForce::IOC) {
                    // IOC订单处理逻辑
                    int64_t best_ask = order_book.get_best_ask();
                    while (order.qty > order.filled_qty && best_ask != -1 && order.price >= best_ask) {
                        Order& best_ask_order = order_book.ask_book[order_book.price_to_index(best_ask)].front();
                        uint32_t trade_qty = std::min(order.qty - order.filled_qty, best_ask_order.qty - best_ask_order.filled_qty);
                        int64_t trade_price = best_ask_order.price; // 价格优先原则，成交价为对手方订单价格
                        order.filled_qty += trade_qty;
                        best_ask_order.filled_qty += trade_qty;

                        // 生成买方成交回报
                        TradeResponse trade_response;
                        trade_response.order_id = order.order_id;
                        trade_response.trader_id = order.trader_id;
                        trade_response.symbol_id = order.symbol_id;
                        trade_response.side = order.side;
                        trade_response.price = trade_price;
                        trade_response.filled_qty = trade_qty;
                        trade_response.status = (order.qty == order.filled_qty) ? OrderStatus::FILLED : OrderStatus::PARTIAL_FILLED;
                        trade_response_queue.enqueue(trade_response); // 发送

                        // 生成卖方成交回报
                        TradeResponse sell_trade_response;
                        sell_trade_response.order_id = best_ask_order.order_id;
                        sell_trade_response.trader_id = best_ask_order.trader_id;
                        sell_trade_response.symbol_id = best_ask_order.symbol_id;
                        sell_trade_response.side = best_ask_order.side;
                        sell_trade_response.price = trade_price;
                        sell_trade_response.filled_qty = trade_qty;
                        sell_trade_response.status = (best_ask_order.qty == best_ask_order.filled_qty) ? OrderStatus::FILLED : OrderStatus::PARTIAL_FILLED;
                        trade_response_queue.enqueue(sell_trade_response); // 发送

                        if (best_ask_order.qty == best_ask_order.filled_qty) {
                            // 对手方订单完全成交，移除订单簿中的订单
                            order_book.order_map.erase(best_ask_order.order_id);
                            order_book.ask_book[order_book.price_to_index(best_ask)].pop_front();
                        }

                        // 寻找下一个最优卖价
                        while (order_book.ask_book[order_book.price_to_index(best_ask)].empty()) {
                            if (best_ask < order_book.get_upper_limit_price()) {
                                best_ask++;
                            } else {
                                best_ask = -1; // 没有更多卖单了
                                break;
                            }
                        }
                    }
                    // IOC订单未完全成交的部分直接丢弃，不加入订单簿
                }
            } else if (order.order_type == OrderType::MARKET) {
                if (order.tif == TimeInForce::IOC) {
                    // 市价IOC订单处理逻辑，类似于上面的限价IOC，但不需要价格比较，直接撮合到最优价格直到数量满足或没有对手单了
                    int64_t best_ask = order_book.get_best_ask();
                    // 5档价格保护
                    int64_t price_limit = (best_ask != -1) ? best_ask + 4 : order_book.get_upper_limit_price(); // 市价单只能吃到5档价格
                    while (order.qty > order.filled_qty && best_ask != -1 && best_ask <= price_limit) {
                        Order& best_ask_order = order_book.ask_book[order_book.price_to_index(best_ask)].front();
                        uint32_t trade_qty = std::min(order.qty - order.filled_qty, best_ask_order.qty - best_ask_order.filled_qty);
                        int64_t trade_price = best_ask_order.price; // 价格优先原则，成交价为对手方订单价格
                        order.filled_qty += trade_qty;
                        best_ask_order.filled_qty += trade_qty;

                        // 生成买方成交回报
                        TradeResponse trade_response;
                        trade_response.order_id = order.order_id;
                        trade_response.trader_id = order.trader_id;
                        trade_response.symbol_id = order.symbol_id;
                        trade_response.side = order.side;
                        trade_response.price = trade_price;
                        trade_response.filled_qty = trade_qty;
                        trade_response.status = (order.qty == order.filled_qty) ? OrderStatus::FILLED : OrderStatus::PARTIAL_FILLED;
                        trade_response_queue.enqueue(trade_response); // 发送

                        // 生成卖方成交回报
                        TradeResponse sell_trade_response;
                        sell_trade_response.order_id = best_ask_order.order_id;
                        sell_trade_response.trader_id = best_ask_order.trader_id;
                        sell_trade_response.symbol_id = best_ask_order.symbol_id;
                        sell_trade_response.side = best_ask_order.side;
                        sell_trade_response.price = trade_price;
                        sell_trade_response.filled_qty = trade_qty;
                        sell_trade_response.status = (best_ask_order.qty == best_ask_order.filled_qty) ? OrderStatus::FILLED : OrderStatus::PARTIAL_FILLED;
                        trade_response_queue.enqueue(sell_trade_response); // 发送

                        if (best_ask_order.qty == best_ask_order.filled_qty) {
                            // 对手方订单完全成交，移除订单簿中的订单
                            order_book.order_map.erase(best_ask_order.order_id);
                            order_book.ask_book[order_book.price_to_index(best_ask)].pop_front();
                        }

                        // 寻找下一个最优卖价
                        while (order_book.ask_book[order_book.price_to_index(best_ask)].empty()) {
                            if (best_ask < order_book.get_upper_limit_price()) {
                                best_ask++;
                            } else {
                                best_ask = -1; // 没有更多卖单了
                                break;
                            }
                        }
                    }
                    // 市价IOC订单未完全成交的部分直接丢弃，不加入订单簿    
                } else {
                    spdlog::error("Unsupported order type for order_id {}: MARKET orders must be IOC", order.order_id);
                }
            } else {
                spdlog::error("Unknown order type for order_id {}: {}", order.order_id, static_cast<uint8_t>(order.order_type));
            }
        } else {
            if (order.order_type == OrderType::LIMIT) {
                if (order.tif == TimeInForce::GTC) {
                    int64_t best_bid = order_book.get_best_bid();
                    spdlog::debug("Trying to match order_id {}: best_bid {}, order_price {}", order.order_id, best_bid, order.price);
                    // 可以撮合，执行撮合逻辑
                    while (order.qty > order.filled_qty && best_bid != -1 && order.price <= best_bid) {
                        Order& best_bid_order = order_book.bid_book[order_book.price_to_index(best_bid)].front();
                        uint32_t trade_qty = std::min(order.qty - order.filled_qty, best_bid_order.qty - best_bid_order.filled_qty);
                        int64_t trade_price = best_bid_order.price; // 价格优先原则，成交价为对手方订单价格
                        order.filled_qty += trade_qty;
                        best_bid_order.filled_qty += trade_qty;

                        // 生成卖方成交回报
                        TradeResponse trade_response;
                        trade_response.order_id = order.order_id;
                        trade_response.trader_id = order.trader_id;
                        trade_response.symbol_id = order.symbol_id;
                        trade_response.side = order.side;
                        trade_response.price = trade_price;
                        trade_response.filled_qty = trade_qty;
                        trade_response.status = (order.qty == order.filled_qty) ? OrderStatus::FILLED : OrderStatus::PARTIAL_FILLED;
                        trade_response_queue.enqueue(trade_response); // 发送
                        spdlog::debug("Generated trade response for order_id {}: filled_qty {}, price {}", order.order_id, trade_response.filled_qty, trade_response.price);

                        // 生成买方成交回报
                        TradeResponse buy_trade_response;
                        buy_trade_response.order_id = best_bid_order.order_id;
                        buy_trade_response.trader_id = best_bid_order.trader_id;
                        buy_trade_response.symbol_id = best_bid_order.symbol_id;
                        buy_trade_response.side = best_bid_order.side;
                        buy_trade_response.price = trade_price;
                        buy_trade_response.filled_qty = trade_qty;
                        buy_trade_response.status = (best_bid_order.qty == best_bid_order.filled_qty) ? OrderStatus::FILLED : OrderStatus::PARTIAL_FILLED;
                        trade_response_queue.enqueue(buy_trade_response); // 发送
                        spdlog::debug("Generated trade response for order_id {}: filled_qty {}, price {}", best_bid_order.order_id, buy_trade_response.filled_qty, buy_trade_response.price);

                        if (best_bid_order.qty == best_bid_order.filled_qty) {
                            // 对手方订单完全成交，移除订单簿中的订单
                            spdlog::debug("Best bid order_id {} fully filled, removing from order book", best_bid_order.order_id);
                            order_book.order_map.erase(best_bid_order.order_id);
                            order_book.bid_book[order_book.price_to_index(best_bid)].pop_front();
                        } 
                        if (order.filled_qty == order.qty) {
                            // 当前订单完全成交，退出循环
                            spdlog::debug("Order_id {} fully filled, exiting match loop", order.order_id);
                            break;
                        }

                        // 寻找下一个最优买价
                        while (order_book.bid_book[order_book.price_to_index(best_bid)].empty()) {
                            if (best_bid > order_book.get_lower_limit_price()) {
                                best_bid--;
                            } else {
                                best_bid = -1; // 没有更多买单了
                                break;
                            }
                        }
                    }
                    if (order.qty > order.filled_qty) {
                        // 还有剩余，加入订单簿
                        spdlog::debug("Order_id {} has remaining qty {}, adding to order book", order.order_id, order.qty - order.filled_qty);
                        order_book.add_order(order);
                    }

                } else if (order.tif == TimeInForce::IOC) {
                    // IOC订单处理逻辑
                    int64_t best_bid = order_book.get_best_bid();
                    while (order.qty > order.filled_qty && best_bid != -1 && order.price <= best_bid) {
                        Order& best_bid_order = order_book.bid_book[order_book.price_to_index(best_bid)].front();
                        uint32_t trade_qty = std::min(order.qty - order.filled_qty, best_bid_order.qty - best_bid_order.filled_qty);
                        int64_t trade_price = best_bid_order.price; // 价格优先原则，成交价为对手方订单价格
                        order.filled_qty += trade_qty;
                        best_bid_order.filled_qty += trade_qty;

                        // 生成卖方成交回报
                        TradeResponse trade_response;
                        trade_response.order_id = order.order_id;
                        trade_response.trader_id = order.trader_id;
                        trade_response.symbol_id = order.symbol_id;
                        trade_response.side = order.side;
                        trade_response.price = trade_price;
                        trade_response.filled_qty = trade_qty;
                        trade_response.status = (order.qty == order.filled_qty) ? OrderStatus::FILLED : OrderStatus::PARTIAL_FILLED;
                        trade_response_queue.enqueue(trade_response); // 发送

                        // 生成买方成交回报
                        TradeResponse buy_trade_response;
                        buy_trade_response.order_id = best_bid_order.order_id;
                        buy_trade_response.trader_id = best_bid_order.trader_id;
                        buy_trade_response.symbol_id = best_bid_order.symbol_id;
                        buy_trade_response.side = best_bid_order.side;
                        buy_trade_response.price = trade_price;
                        buy_trade_response.filled_qty = trade_qty;
                        buy_trade_response.status = (best_bid_order.qty == best_bid_order.filled_qty) ? OrderStatus::FILLED : OrderStatus::PARTIAL_FILLED;
                        trade_response_queue.enqueue(buy_trade_response); // 发送

                        if (best_bid_order.qty == best_bid_order.filled_qty) {
                            // 对手方订单完全成交，移除订单簿中的订单
                            order_book.order_map.erase(best_bid_order.order_id);
                            order_book.bid_book[order_book.price_to_index(best_bid)].pop_front();
                        }

                        // 寻找下一个最优买价
                        while (order_book.bid_book[order_book.price_to_index(best_bid)].empty()) {
                            if (best_bid > order_book.get_lower_limit_price()) {
                                best_bid--;
                            } else {
                                best_bid = -1; // 没有更多买单了
                                break;
                            }
                        }
                    }
                    // IOC订单未完全成交的部分直接丢弃，不加入订单簿

                }
            } else if (order.order_type == OrderType::MARKET) {
                if (order.tif == TimeInForce::IOC) {
                    // 市价IOC订单处理逻辑，类似于上面的限价IOC，但不需要价格比较，直接撮合到最优价格直到数量满足或没有对手单了
                    int64_t best_bid = order_book.get_best_bid();
                    // 5档价格保护
                    int64_t price_limit = (best_bid != -1) ? best_bid - 4 : order_book.get_lower_limit_price(); // 市价单只能吃到5档价格
                    while (order.qty > order.filled_qty && best_bid != -1 && best_bid >= price_limit) {
                        Order& best_bid_order = order_book.bid_book[order_book.price_to_index(best_bid)].front();
                        uint32_t trade_qty = std::min(order.qty - order.filled_qty, best_bid_order.qty - best_bid_order.filled_qty);
                        int64_t trade_price = best_bid_order.price; // 价格优先原则，成交价为对手方订单价格
                        order.filled_qty += trade_qty;
                        best_bid_order.filled_qty += trade_qty;

                        // 生成卖方成交回报
                        TradeResponse trade_response;
                        trade_response.order_id = order.order_id;
                        trade_response.trader_id = order.trader_id;
                        trade_response.symbol_id = order.symbol_id;
                        trade_response.side = order.side;
                        trade_response.price = trade_price;
                        trade_response.filled_qty = trade_qty;
                        trade_response.status = (order.qty == order.filled_qty) ? OrderStatus::FILLED : OrderStatus::PARTIAL_FILLED;
                        trade_response_queue.enqueue(trade_response); // 发送

                        // 生成买方成交回报
                        TradeResponse buy_trade_response;
                        buy_trade_response.order_id = best_bid_order.order_id;
                        buy_trade_response.trader_id = best_bid_order.trader_id;
                        buy_trade_response.symbol_id = best_bid_order.symbol_id;
                        buy_trade_response.side = best_bid_order.side;
                        buy_trade_response.price = trade_price;
                        buy_trade_response.filled_qty = trade_qty;
                        buy_trade_response.status = (best_bid_order.qty == best_bid_order.filled_qty) ? OrderStatus::FILLED : OrderStatus::PARTIAL_FILLED;
                        trade_response_queue.enqueue(buy_trade_response); // 发送

                        if (best_bid_order.qty == best_bid_order.filled_qty) {
                            // 对手方订单完全成交，移除订单簿中的订单
                            order_book.order_map.erase(best_bid_order.order_id);
                            order_book.bid_book[order_book.price_to_index(best_bid)].pop_front();
                            
                        }

                        // 寻找下一个最优买价
                        while (order_book.bid_book[order_book.price_to_index(best_bid)].empty()) {
                            if (best_bid > order_book.get_lower_limit_price()) {
                                best_bid--;
                            } else {
                                best_bid = -1; // 没有更多买单了
                                break;
                            }
                        }
                    }
                    // 市价IOC订单未完全成交的部分直接丢弃，不加入订单簿

                } else {
                    spdlog::error("Unsupported order type for order_id {}: MARKET orders must be IOC", order.order_id);
                }
            } else {
                spdlog::error("Unknown order type for order_id {}: {}", order.order_id, static_cast<uint8_t>(order.order_type));
            }
        }
    }

    void try_cancel(const CancelRequest& cancel_req) {
        bool canceled = order_book.cancel_order(cancel_req.order_id);
        TradeResponse resp;
        resp.order_id = cancel_req.order_id;
        resp.trader_id = cancel_req.trader_id;
        resp.symbol_id = cancel_req.symbol_id;
        resp.side = Side::BUY; // 撤单方向无法直接获取，暂时置为BUY
        resp.price = 0;
        resp.filled_qty = 0;
        resp.status = canceled ? OrderStatus::CANCELED : OrderStatus::REJECTED;
        trade_response_queue.enqueue(resp);
        spdlog::info("Cancel order {} result: {}", cancel_req.order_id, canceled ? "CANCELED" : "REJECTED");
    }
    
};

