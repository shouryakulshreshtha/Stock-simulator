// Bots.h - the automatic traders that create the order flow.
// Kaggle price files hold prices only, never orders. These bots are where the buy and sell orders come from.
#pragma once
#include <map>
#include <string>
#include <vector>

#include "Participant.h"
#include "Utils.h"

// BotTrader = Trader + Auditable (multiple inheritance). Holds what all bots share.
class BotTrader : public Trader, public Auditable {
public:
    BotTrader(std::string id, std::string name, int intervalMillis, unsigned seed, std::vector<std::string> symbols);

protected:
    const std::string& pickSymbol(std::mt19937& rng) const;
    Command limitOrder(const std::string& symbol, Side side, int quantity, Paise price);
    Command marketOrder(const std::string& symbol, Side side, int quantity, Paise protectionPrice);
    Command cancelOrder(const std::string& symbol, OrderId id);
    static Paise around(Paise reference, double fraction);  // reference * (1 + fraction), on a valid tick

    std::vector<std::string> symbols_;
};

// Random orders close to the reference price. Mostly limit orders, sometimes market orders, sometimes cancels.
class RandomBot : public BotTrader {
public:
    RandomBot(std::string id, int intervalMillis, unsigned seed, std::vector<std::string> symbols);
    std::vector<Command> decide(const MarketState& market, std::mt19937& rng) override;

private:
    std::vector<std::pair<std::string, OrderId>> myOrders_;  // to pick something to cancel
};

// Buys when the price is above its recent average, sells when it is below (follows the trend).
class MomentumBot : public BotTrader {
public:
    MomentumBot(std::string id, int intervalMillis, unsigned seed, std::vector<std::string> symbols);
    std::vector<Command> decide(const MarketState& market, std::mt19937& rng) override;

private:
    std::map<std::string, RollingAverage<Paise>> averages_;  // class template in use
};

// Keeps a bid below and an ask above the reference price, so there is always someone to trade with.
class MarketMakerBot : public BotTrader {
public:
    MarketMakerBot(std::string id, int intervalMillis, unsigned seed, std::vector<std::string> symbols);
    std::vector<Command> decide(const MarketState& market, std::mt19937& rng) override;

private:
    std::map<std::string, std::pair<OrderId, OrderId>> quotes_;  // last bid id, last ask id per symbol
};
