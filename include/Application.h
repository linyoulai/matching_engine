#pragma once
#include "MatchingEngine.h"
#include "OrderManager.h"
#include "TradeManager.h"
#include "AccountManager.h"
#include "RiskControlManager.h"
#include "MarketDataService.h"
#include "RestApiService.h"
#include "WebSocketApiService.h"
#include "PersistenceService.h"
#include <memory>

// 系统管理者/应用管理器，统一启动和管理所有核心模块
class Application {
public:
    explicit Application();
    void init();      // 初始化所有模块
    void run();       // 启动主流程（如接口监听、撮合主循环等）
    void shutdown();  // 优雅关闭，释放资源
private:
    std::unique_ptr<MatchingEngine> matching_engine_;
    std::unique_ptr<OrderManager> order_manager_;
    std::unique_ptr<TradeManager> trade_manager_;
    std::unique_ptr<AccountManager> account_manager_;
    std::unique_ptr<RiskControlManager> risk_control_manager_;
    std::unique_ptr<MarketDataService> market_data_service_;
    std::unique_ptr<RestApiService> rest_api_service_;
    std::unique_ptr<WebSocketApiService> ws_api_service_;
    std::unique_ptr<PersistenceService> persistence_service_;
    // ...可扩展更多模块
};
