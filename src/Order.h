Z// Order.h - the Order class hierarchy.
//
// Order is an ABSTRACT base class (it has pure virtual functions), so it cannot be created on its own.
// LimitOrder and MarketOrder derive from it. Code that only knows "an Order" calls virtual functions
// and the right version runs at run time: this is run-time polymorphism.
#pragma once
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>

#include "Types.h"

class Order {
public:
    Order(std::string symbol, Side side, int quantity, std::string traderId);
    Order(const Order&) = default;
    Order& operator=(const Order&) = default;
    virtual ~Order() = default;  // virtual: deleting a LimitOrder through an Order* must run the right destructor

    // ---- pure virtual functions: every kind of order must provide these ----
    virtual std::string type() const = 0;
    virtual std::string describe() const = 0;
    virtual Paise limitPrice() const = 0;               // price limit (for a market order: its protection price)
    virtual bool crosses(Paise oppositePrice) const = 0;  // would this order trade at that price?
    virtual bool isMarket() const { return false; }

    // ---- getters (data is protected, so outside code reads it through these) ----
    OrderId id() const { return id_; }
    const std::string& symbol() const { return symbol_; }
    Side side() const { return side_; }
    int quantity() const { return quantity_; }  // quantity still unfilled
    const std::string& traderId() const { return traderId_; }
    std::uint64_t arrival() const { return arrival_; }

    void reduce(int filled);  // called by the order book after a trade
    void setArrival(std::uint64_t sequence) { arrival_ = sequence; }

    // Operator overloading: "a < b" means a arrived earlier, i.e. a has time priority over b.
    bool operator<(const Order& other) const { return arrival_ < other.arrival_; }

    // Overloaded new and delete: count the Order objects that are alive, to prove nothing leaks.
    static void* operator new(std::size_t size);
    static void operator delete(void* pointer) noexcept;
    static long liveOrders() { return liveCount_.load(); }

    // Friend: the stream is the LEFT operand of <<, so it cannot be a member; friend lets it use our data.
    friend std::ostream& operator<<(std::ostream& os, const Order& order);

protected:
    OrderId id_;
    std::string symbol_;
    Side side_;
    int quantity_;
    std::string traderId_;
    std::uint64_t arrival_{0};

private:
    static std::atomic<OrderId> nextId_;
    static std::atomic<long> liveCount_;
};

// Trades at its price or better, otherwise waits in the order book.
class LimitOrder : public Order {
public:
    LimitOrder(std::string symbol, Side side, int quantity, Paise price, std::string traderId);

    std::string type() const override { return "LIMIT"; }
    std::string describe() const override;
    Paise limitPrice() const override { return price_; }
    bool crosses(Paise oppositePrice) const override;

private:
    Paise price_;
};

// Trades immediately at the best prices available. It carries a protection price so one thin
// order book cannot make it trade at a crazy price; whatever is not filled is dropped.
class MarketOrder : public Order {
public:
    MarketOrder(std::string symbol, Side side, int quantity, Paise protectionPrice, std::string traderId);

    std::string type() const override { return "MARKET"; }
    std::string describe() const override;
    Paise limitPrice() const override { return protection_; }
    bool crosses(Paise oppositePrice) const override;
    bool isMarket() const override { return true; }

private:
    Paise protection_;
};
