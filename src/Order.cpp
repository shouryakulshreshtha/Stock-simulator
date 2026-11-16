#include "Order.h"

#include <iomanip>
#include <new>
#include <ostream>
#include <sstream>

#include "Errors.h"
#include "Utils.h"

std::atomic<OrderId> Order::nextId_{1};
std::atomic<long> Order::liveCount_{0};

Order::Order(std::string symbol, Side side, int quantity, std::string traderId)
    : id_(nextId_.fetch_add(1)),
      symbol_(std::move(symbol)),
      side_(side),
      quantity_(quantity),
      traderId_(std::move(traderId)) {
    if (symbol_.empty()) throw InvalidOrderError("symbol is empty");
    if (quantity_ <= 0) throw InvalidOrderError("quantity must be positive");
    if (traderId_.empty()) throw InvalidOrderError("trader id is empty");
}

void Order::reduce(int filled) {
    if (filled <= 0 || filled > quantity_) throw InvalidOrderError("bad fill quantity");
    quantity_ -= filled;
}

void* Order::operator new(std::size_t size) {
    void* memory = ::operator new(size);  // global allocator does the real work
    ++liveCount_;
    return memory;
}

void Order::operator delete(void* pointer) noexcept {
    if (pointer == nullptr) return;
    --liveCount_;
    ::operator delete(pointer);
}

std::ostream& operator<<(std::ostream& os, const Order& order) { return os << order.describe(); }

// ---------------- LimitOrder ----------------

LimitOrder::LimitOrder(std::string symbol, Side side, int quantity, Paise price, std::string traderId)
    : Order(std::move(symbol), side, quantity, std::move(traderId)), price_(price) {
    if (price_ <= 0) throw InvalidOrderError("price must be positive");
    if (price_ % TICK_PAISE != 0) throw InvalidOrderError("price must be a multiple of 0.05");
}

std::string LimitOrder::describe() const {
    std::ostringstream os;
    os << '#' << id_ << " LIMIT " << toString(side_) << ' ' << quantity_ << ' ' << symbol_ << " @ " << formatPrice(price_)
       << " [" << traderId_ << ']';
    return os.str();
}

bool LimitOrder::crosses(Paise oppositePrice) const {
    return side_ == Side::Buy ? oppositePrice <= price_ : oppositePrice >= price_;
}

// ---------------- MarketOrder ----------------

MarketOrder::MarketOrder(std::string symbol, Side side, int quantity, Paise protectionPrice, std::string traderId)
    : Order(std::move(symbol), side, quantity, std::move(traderId)), protection_(protectionPrice) {
    if (protection_ <= 0) throw InvalidOrderError("protection price must be positive");
    if (protection_ % TICK_PAISE != 0) throw InvalidOrderError("price must be a multiple of 0.05");
}

std::string MarketOrder::describe() const {
    std::ostringstream os;
    os << '#' << id_ << " MARKET " << toString(side_) << ' ' << quantity_ << ' ' << symbol_ << " (limit "
       << formatPrice(protection_) << ") [" << traderId_ << ']';
    return os.str();
}

bool MarketOrder::crosses(Paise oppositePrice) const {
    return side_ == Side::Buy ? oppositePrice <= protection_ : oppositePrice >= protection_;
}
