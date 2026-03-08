// include/MatchingEngine.h
#pragma once
#include "../lib/concurrentqueue.h"
#include "Response.h"
#include "Request.h"
#include "Order.h"
#inlcude "OrderBook.h"

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
                    try_macth(order);
                } else if (req_env.req_type == RequestType::CANCEL) {
                    // 处理撤单请求
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

    void try_macth(Order& order) {
        if (order.side == Side::BUY) {
            if (order.order_type == OrderType::LIMIT) {
                if (order.tif == TimeInForce::GTC) {

                } else if (order.tif == TimeInForce::IOC) {

                } else if (order.tif == TimeInForce::FOK) {

                }
            } else {
                if (order.tif == TimeInForce::GTC) {

                } else if (order.tif == TimeInForce::IOC) {

                } else if (order.tif == TimeInForce::FOK) {

                }
            }
        } else {
            if (order.order_type == OrderType::LIMIT) {
                if (order.tif == TimeInForce::GTC) {

                } else if (order.tif == TimeInForce::IOC) {

                } else if (order.tif == TimeInForce::FOK) {

                }
            } else {
                if (order.tif == TimeInForce::GTC) {

                } else if (order.tif == TimeInForce::IOC) {

                } else if (order.tif == TimeInForce::FOK) {

                }
            }
        }
    }

    void try_cancel(const CancelRequest& cancel_req) {

    }
    
};

