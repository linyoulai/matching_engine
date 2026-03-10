// main.cpp
#include "Gateway.h"
#include <spdlog/spdlog.h>
#include <iostream>
#include <string>
#include "concurrentqueue.h"
#include "MatchingEngine.h"

/*
6.主函数：
    创建order_queue, trade_response_queue, market_data_queue
    创建撮合引擎对象，传入队列引用
    创建网关对象，传入队列引用
*/

int main(int argc, char** argv) {
    spdlog::set_level(spdlog::level::debug);
    spdlog::info("System Starting...");

    try {
        moodycamel::ConcurrentQueue<RequestEnvelope> order_queue; // 下单、撤单队列
        moodycamel::ConcurrentQueue<TradeResponse> trade_response_queue; // 成交回报队列

        moodycamel::ConcurrentQueue<MarketDataResponse> market_data_queue; // 行情推送队列

        MatchingEngine engine(order_queue, trade_response_queue, market_data_queue);
        Gateway gateway(order_queue, trade_response_queue, market_data_queue);

        if (argc > 1 && std::string(argv[1]) == "--stress") {
            // 压测模式下关闭 debug 洪泛日志，避免淹没最终统计摘要
            spdlog::set_level(spdlog::level::warn);
            int thread_count = 100;
            int ops_per_thread = 20000;
            if (argc > 2) {
                thread_count = std::stoi(argv[2]);
            }
            if (argc > 3) {
                ops_per_thread = std::stoi(argv[3]);
            }

            spdlog::info("Run stress mode: threads={}, ops_per_thread={}", thread_count, ops_per_thread);
            gateway.stress_submit_cancel(thread_count, ops_per_thread);

            // 给撮合线程和回报线程一点时间清空队列
            std::this_thread::sleep_for(std::chrono::seconds(2));
            spdlog::info("Stress mode finished, exiting...");
            return 0;
        }

        std::this_thread::sleep_for(std::chrono::seconds(1)); // 当前线程休眠
        gateway.mock_submit_and_cancel(); // 模拟一些下单和撤单请求

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