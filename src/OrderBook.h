// OrderBook.h - the bids and asks of ONE stock, matched by price-time priority.
//
//   bids_ : buy orders,  best (highest) price first    std::map with std::greater
//   asks_ : sell orders, best (lowest)  price first    std::map
//   each price level is a std::deque, so the oldest order is always at the front (time priority)
#pragma once
#include <deque>
#include <functional>
#include <iosfwd>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Order.h"
#include "Trade.h"

struct BookLevel {
    Paise price{0};
    int quantity{0};  // total quantity at this price
    int orders{0};    // number of orders at this price
};

struct BookSnapshot {
    std::string symbol;
    std::vector<BookLevel> bids;  // best first
    std::vector<BookLevel> asks;  // best first
};

// What happened when one order was submitted.
struct MatchResult {
    std::vector<Trade> trades;
    std::vector<std::unique_ptr<Order>> cancelled;  // resting orders removed by self-trade prevention
    std::unique_ptr<Order> dropped;                 // market order that could not be filled completely
    bool rested{false};                             // limit order that now waits in the book
};

class OrderBook {
public:
    explicit OrderBook(std::string symbol);

    // Match the order against the book. Takes ownership of the order.
    MatchResult submit(std::unique_ptr<Order> order, std::uint64_t arrival);

    // Remove an order by id. Returns nullptr when the id is not in the book.
    std::unique_ptr<Order> cancel(OrderId id);
    const Order* find(OrderId id) const;

    BookSnapshot snapshot(std::size_t depth) const;
    Paise bestBid() const;  // 0 when there are no bids
    Paise bestAsk() const;  // 0 when there are no asks
    std::size_t openOrders() const { return index_.size(); }
    const std::string& symbol() const { return symbol_; }

    // Self-check used by the tests and the engine: sorted levels, no crossed book, index matches.
    bool checkInvariants(std::string* why = nullptr) const;

    friend std::ostream& operator<<(std::ostream& os, const OrderBook& book);

private:
    using OrderQueue = std::deque<std::unique_ptr<Order>>;

    // Function template: the same matching loop works for the bid side and the ask side.
    template <typename BookSide>
    void matchAgainst(BookSide& opposite, Order& incoming, MatchResult& result);

    std::string symbol_;
    std::map<Paise, OrderQueue, std::greater<Paise>> bids_;
    std::map<Paise, OrderQueue> asks_;
    std::unordered_map<OrderId, std::pair<Side, Paise>> index_;  // where each resting order lives
    std::uint64_t tradeSequence_{0};
};
