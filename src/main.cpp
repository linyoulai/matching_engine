// main.cpp
#include "Gateway.h"
#include <spdlog/spdlog.h>
#include <iostream>

int main() {
    spdlog::set_level(spdlog::level::debug);
    spdlog::info("Trading System Starting...");

    try {
        Gateway gateway;
        spdlog::info("System is up and running. Press Enter to exit.");
        
        while (true) {
            spdlog::info("heart beat");
            sleep(10);
        }
    } catch (const std::exception& e) {
        spdlog::error("System crashed: {}", e.what());
        return 1;
    }

    spdlog::info("System shutdown complete.");
    return 0;
}