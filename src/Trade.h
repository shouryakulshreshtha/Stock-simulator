// Trade.h - one match between a buy order and a sell order.
#pragma once
#include <chrono>
#include <cstdint>
#include <iosfwd>
#include <string>

#include "Types.h"

class Trade {
public:
    Trade(std::string symbol, std::uint64_t sequence, OrderId buyOrderId, OrderId sellOrderId, std::string buyer,
          std::string seller, int quantity, Paise price);

    // Copy and move constructors: a Trade is a plain value, so the compiler-made versions are right.
    Trade(const Trade&) = default;
    Trade(Trade&&) noexcept = default;
    Trade& operator=(const Trade&) = default;
    Trade& operator=(Trade&&) noexcept = default;

    const std::string& symbol() const { return symbol_; }
    std::uint64_t sequence() const { return sequence_; }
    OrderId buyOrderId() const { return buyOrderId_; }
    OrderId sellOrderId() const { return sellOrderId_; }
    const std::string& buyer() const { return buyer_; }
    const std::string& seller() const { return seller_; }
    int quantity() const { return quantity_; }
    Paise price() const { return price_; }
    Paise value() const { return price_ * quantity_; }
    std::string timeString() const;

    friend std::ostream& operator<<(std::ostream& os, const Trade& trade);

private:
    std::string symbol_;
    std::uint64_t sequence_;
    OrderId buyOrderId_;
    OrderId sellOrderId_;
    std::string buyer_;
    std::string seller_;
    int quantity_;
    Paise price_;
    std::chrono::system_clock::time_point time_;
};
