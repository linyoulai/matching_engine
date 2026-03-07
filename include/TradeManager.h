#pragma once
#include "Trade.h"
#include <vector>
#include <cstdint>
#include <optional>

// TradeManager: Manages all trade records
class TradeManager {
public:
    explicit TradeManager();
    void addTrade(const Trade& trade);
    std::optional<Trade> getTrade(uint64_t trade_id) const;
    std::vector<Trade> getTradesByOrder(uint64_t order_id) const;
    std::vector<Trade> getAllTrades() const;
    // Extendable: export, persist, event notification, etc.
private:
    std::vector<Trade> trades_;
    // Extendable: index, persistence, etc.
};
