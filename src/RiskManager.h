// RiskManager.h - pre-trade risk checks. An order only reaches the order book if:
//   1. its price is inside the price band around the reference price (default +-20%), and
//   2. the trader has the cash (buy) or the shares (sell); that cash or those shares are then locked.
#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "MarketState.h"
#include "Portfolio.h"

class RiskManager {
public:
    explicit RiskManager(const MarketState& market, int bandPercent = 20);

    // Setup only (before any thread starts): create the portfolio of a trader.
    Portfolio& addTrader(const std::string& traderId, Paise startingCash);
    Portfolio* find(const std::string& traderId) const;
    std::vector<Portfolio*> portfolios() const;

    void check(const Order& order);    // throws RiskError; on success the cash or shares are reserved
    void release(const Order& order);  // unlock what is left of the order

    int bandPercent() const { return bandPercent_; }

private:
    const MarketState& market_;
    int bandPercent_;
    std::map<std::string, std::unique_ptr<Portfolio>> accounts_;  // the map itself never changes after setup
};
