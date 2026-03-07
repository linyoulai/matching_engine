#include "Order.h"
#include "OrderBook.h"
#include "MatchingEngine.h"
#include <spdlog/spdlog.h>
#include "Application.h"


int main() {

    Application app;
    app.init();
    spdlog::info("app initialized successfully");

    app.run();
    spdlog::info("app is running");

    app.shutdown();
    spdlog::info("app shutdown completed");
    return 0;
}
