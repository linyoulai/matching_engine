// main.cpp
#include "Gateway.h"
#include <spdlog/spdlog.h>
#include <iostream>
#include "concurrentqueue.h"
#include "MatchingEngine.h"

/*
6.主函数：
    创建order_queue, trade_response_queue, market_data_queue
    创建撮合引擎对象，传入队列引用
    创建网关对象，传入队列引用
*/

int main() {
    spdlog::set_level(spdlog::level::debug);
    spdlog::info("System Starting...");

    try {
        moodycamel::ConcurrentQueue<RequestEnvelope> order_queue; // 下单、撤单队列
        moodycamel::ConcurrentQueue<TradeResponse> trade_response_queue; // 成交回报队列

        moodycamel::ConcurrentQueue<MarketDataResponse> market_data_queue; // 行情推送队列

        MatchingEngine engine(order_queue, trade_response_queue, market_data_queue);
        Gateway gateway(order_queue, trade_response_queue, market_data_queue);

        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(1)); // 当前线程休眠
            // sleep(1); // 当前进程休眠
        }
        
    } catch (const std::exception& e) {
        spdlog::error("System crashed: {}", e.what());
        return 1;
    }

    spdlog::info("System shutdown complete.");
    return 0;
}