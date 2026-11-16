#include "Portfolio.h"

#include "Errors.h"
#include "Utils.h"

Portfolio::Portfolio(std::string owner, Paise startingCash)
    : owner_(std::move(owner)), cash_(startingCash), startingEquity_(startingCash) {}

Portfolio::Portfolio(const Portfolio& other) : TradeListener() {
    std::lock_guard<std::mutex> lock(other.mutex_);
    owner_ = other.owner_;
    cash_ = other.cash_;
    reservedCash_ = other.reservedCash_;
    startingEquity_ = other.startingEquity_;
    shares_ = other.shares_;
    reservedShares_ = other.reservedShares_;
    holds_ = other.holds_;
}

Portfolio::Portfolio(Portfolio&& other) noexcept : TradeListener() {
    std::lock_guard<std::mutex> lock(other.mutex_);
    owner_ = std::move(other.owner_);
    cash_ = other.cash_;
    reservedCash_ = other.reservedCash_;
    startingEquity_ = other.startingEquity_;
    shares_ = std::move(other.shares_);
    reservedShares_ = std::move(other.reservedShares_);
    holds_ = std::move(other.holds_);
}

void Portfolio::addShares(const std::string& symbol, int quantity) {
    std::lock_guard<std::mutex> lock(mutex_);
    shares_[symbol] += quantity;
}

void Portfolio::markStart(const std::map<std::string, Paise>& marks) {
    std::lock_guard<std::mutex> lock(mutex_);
    Paise value = cash_;
    for (const auto& [symbol, quantity] : shares_) {
        auto mark = marks.find(symbol);
        if (mark != marks.end()) value += mark->second * quantity;
    }
    startingEquity_ = value;
}

Portfolio& Portfolio::operator+=(const Trade& trade) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string& symbol = trade.symbol();

    if (trade.buyer() == owner_) {
        cash_ -= trade.value();
        shares_[symbol] += trade.quantity();
        auto hold = holds_.find(trade.buyOrderId());
        if (hold != holds_.end()) {
            reservedCash_ -= hold->second.perShare * trade.quantity();
            hold->second.quantityLeft -= trade.quantity();
            if (hold->second.quantityLeft <= 0) holds_.erase(hold);
        }
    }
    if (trade.seller() == owner_) {
        cash_ += trade.value();
        shares_[symbol] -= trade.quantity();
        auto hold = holds_.find(trade.sellOrderId());
        if (hold != holds_.end()) {
            reservedShares_[symbol] -= trade.quantity();
            hold->second.quantityLeft -= trade.quantity();
            if (hold->second.quantityLeft <= 0) holds_.erase(hold);
        }
    }
    return *this;
}

void Portfolio::onTrade(const Trade& trade) {
    if (trade.buyer() == owner_ || trade.seller() == owner_) *this += trade;
}

void Portfolio::reserve(const Order& order) {
    std::lock_guard<std::mutex> lock(mutex_);
    const int quantity = order.quantity();

    if (order.side() == Side::Buy) {
        const Paise needed = order.limitPrice() * quantity;
        const Paise available = cash_ - reservedCash_;
        if (available < needed)
            throw RiskError(owner_ + ": not enough cash (need " + formatPrice(needed) + ", free " + formatPrice(available) + ")");
        reservedCash_ += needed;
    } else {
        const int owned = shares_.count(order.symbol()) ? shares_[order.symbol()] : 0;
        const int locked = reservedShares_.count(order.symbol()) ? reservedShares_[order.symbol()] : 0;
        if (owned - locked < quantity)
            throw RiskError(owner_ + ": not enough " + order.symbol() + " shares (need " + std::to_string(quantity) +
                            ", free " + std::to_string(owned - locked) + ")");
        reservedShares_[order.symbol()] += quantity;
    }
    holds_[order.id()] = Hold{order.side(), order.symbol(), order.limitPrice(), quantity};
}

void Portfolio::release(const Order& order) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto hold = holds_.find(order.id());
    if (hold == holds_.end()) return;  // nothing locked (already fully filled)
    if (hold->second.side == Side::Buy)
        reservedCash_ -= hold->second.perShare * hold->second.quantityLeft;
    else
        reservedShares_[hold->second.symbol] -= hold->second.quantityLeft;
    holds_.erase(hold);
}

Portfolio::Summary Portfolio::summary(const std::map<std::string, Paise>& marks) const {
    std::lock_guard<std::mutex> lock(mutex_);
    Summary s;
    s.owner = owner_;
    s.cash = cash_;
    for (const auto& [symbol, quantity] : shares_) {
        auto mark = marks.find(symbol);
        if (mark != marks.end()) s.holdingsValue += mark->second * quantity;
    }
    s.equity = s.cash + s.holdingsValue;
    s.pnl = s.equity - startingEquity_;
    return s;
}

Paise Portfolio::cash() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cash_;
}

Paise Portfolio::reservedCash() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return reservedCash_;
}

int Portfolio::shares(const std::string& symbol) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto found = shares_.find(symbol);
    return found == shares_.end() ? 0 : found->second;
}

int Portfolio::reservedShares(const std::string& symbol) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto found = reservedShares_.find(symbol);
    return found == reservedShares_.end() ? 0 : found->second;
}
