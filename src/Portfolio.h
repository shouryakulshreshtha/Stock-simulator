// Portfolio.h - cash and shares of one trader.
//
// A Portfolio is also a TradeListener (Observer pattern): the engine tells it about every trade and it
// updates itself when its owner was the buyer or the seller.
//
// Reservations: when an order is accepted its cash (buy) or shares (sell) are locked, so the same money
// cannot be promised to two orders. Trades and cancels release the lock.
#pragma once
#include <map>
#include <mutex>
#include <string>

#include "Order.h"
#include "TradeListener.h"

class Portfolio : public TradeListener {
public:
    struct Summary {
        std::string owner;
        Paise cash{0};
        Paise holdingsValue{0};
        Paise equity{0};
        Paise pnl{0};
    };

    Portfolio(std::string owner, Paise startingCash);  // parameterized constructor
    Portfolio(const Portfolio& other);                 // copy constructor: copies the data, gets its own mutex
    Portfolio(Portfolio&& other) noexcept;             // move constructor
    Portfolio& operator=(const Portfolio&) = delete;
    ~Portfolio() override = default;

    void addShares(const std::string& symbol, int quantity);  // initial holdings
    void markStart(const std::map<std::string, Paise>& marks);  // remember starting equity for the P&L

    // Operator overloading: portfolio += trade applies the trade to cash and shares.
    Portfolio& operator+=(const Trade& trade);
    void onTrade(const Trade& trade) override;  // Observer: called by the matching engine

    void reserve(const Order& order);  // throws RiskError when cash or shares are not enough
    void release(const Order& order);  // give back whatever is still locked for this order

    Summary summary(const std::map<std::string, Paise>& marks) const;
    Paise cash() const;
    Paise reservedCash() const;
    int shares(const std::string& symbol) const;
    int reservedShares(const std::string& symbol) const;
    const std::string& owner() const { return owner_; }

private:
    struct Hold {
        Side side;
        std::string symbol;
        Paise perShare;
        int quantityLeft;
    };

    mutable std::mutex mutex_;  // engine thread writes, dashboard thread reads
    std::string owner_;
    Paise cash_;  // total cash; reservedCash_ is the part of it that is locked
    Paise reservedCash_{0};
    Paise startingEquity_{0};
    std::map<std::string, int> shares_;
    std::map<std::string, int> reservedShares_;
    std::map<OrderId, Hold> holds_;
};
