#include "OrderBook.h"

#include <algorithm>
#include <iomanip>
#include <ostream>

#include "Errors.h"
#include "Utils.h"

OrderBook::OrderBook(std::string symbol) : symbol_(std::move(symbol)) {}

template <typename BookSide>
void OrderBook::matchAgainst(BookSide& opposite, Order& incoming, MatchResult& result) {
    while (incoming.quantity() > 0 && !opposite.empty()) {
        auto levelIt = opposite.begin();  // best price on the other side
        const Paise levelPrice = levelIt->first;
        if (!incoming.crosses(levelPrice)) break;  // best price is not good enough: stop

        OrderQueue& queue = levelIt->second;
        while (incoming.quantity() > 0 && !queue.empty()) {
            Order& resting = *queue.front();  // oldest order at this price: time priority

            // Self-trade prevention: a trader's order must not trade with the same trader's resting order.
            if (resting.traderId() == incoming.traderId()) {
                index_.erase(resting.id());
                result.cancelled.push_back(std::move(queue.front()));
                queue.pop_front();
                continue;
            }

            const int quantity = std::min(incoming.quantity(), resting.quantity());  // partial fills happen here
            incoming.reduce(quantity);
            resting.reduce(quantity);

            const bool incomingIsBuy = incoming.side() == Side::Buy;
            const Order& buy = incomingIsBuy ? incoming : resting;
            const Order& sell = incomingIsBuy ? resting : incoming;
            // The trade happens at the price of the order that was waiting in the book.
            result.trades.emplace_back(symbol_, ++tradeSequence_, buy.id(), sell.id(), buy.traderId(), sell.traderId(),
                                       quantity, levelPrice);

            if (resting.quantity() == 0) {
                index_.erase(resting.id());
                queue.pop_front();
            }
        }
        if (queue.empty()) opposite.erase(levelIt);
    }
}

MatchResult OrderBook::submit(std::unique_ptr<Order> order, std::uint64_t arrival) {
    if (!order) throw InvalidOrderError("empty order");
    if (order->symbol() != symbol_) throw InvalidOrderError("order sent to the wrong order book");

    order->setArrival(arrival);
    MatchResult result;

    if (order->side() == Side::Buy)
        matchAgainst(asks_, *order, result);
    else
        matchAgainst(bids_, *order, result);

    if (order->quantity() > 0) {
        if (order->isMarket()) {
            result.dropped = std::move(order);  // a market order never waits in the book
        } else {
            const Paise price = order->limitPrice();
            const OrderId id = order->id();
            const Side side = order->side();
            OrderQueue& level = (side == Side::Buy) ? bids_[price] : asks_[price];
            level.push_back(std::move(order));  // back of the queue: later orders wait behind earlier ones
            index_[id] = {side, price};
            result.rested = true;
        }
    }
    return result;
}

std::unique_ptr<Order> OrderBook::cancel(OrderId id) {
    auto found = index_.find(id);
    if (found == index_.end()) return nullptr;
    const Side side = found->second.first;
    const Paise price = found->second.second;

    auto removeFrom = [&](auto& bookSide) -> std::unique_ptr<Order> {
        auto level = bookSide.find(price);
        if (level == bookSide.end()) return nullptr;
        OrderQueue& queue = level->second;
        for (auto it = queue.begin(); it != queue.end(); ++it) {
            if ((*it)->id() == id) {
                std::unique_ptr<Order> removed = std::move(*it);
                queue.erase(it);
                if (queue.empty()) bookSide.erase(level);
                return removed;
            }
        }
        return nullptr;
    };

    std::unique_ptr<Order> removed = (side == Side::Buy) ? removeFrom(bids_) : removeFrom(asks_);
    index_.erase(found);
    return removed;
}

const Order* OrderBook::find(OrderId id) const {
    auto found = index_.find(id);
    if (found == index_.end()) return nullptr;
    const Paise price = found->second.second;

    auto search = [&](const auto& bookSide) -> const Order* {
        auto level = bookSide.find(price);
        if (level == bookSide.end()) return nullptr;
        for (const auto& order : level->second)
            if (order->id() == id) return order.get();
        return nullptr;
    };
    return found->second.first == Side::Buy ? search(bids_) : search(asks_);
}

BookSnapshot OrderBook::snapshot(std::size_t depth) const {
    BookSnapshot snap;
    snap.symbol = symbol_;
    auto copyLevels = [&](const auto& bookSide, std::vector<BookLevel>& out) {
        for (const auto& [price, queue] : bookSide) {
            if (out.size() >= depth) break;
            BookLevel level;
            level.price = price;
            level.orders = static_cast<int>(queue.size());
            for (const auto& order : queue) level.quantity += order->quantity();
            out.push_back(level);
        }
    };
    copyLevels(bids_, snap.bids);
    copyLevels(asks_, snap.asks);
    return snap;
}

Paise OrderBook::bestBid() const { return bids_.empty() ? 0 : bids_.begin()->first; }
Paise OrderBook::bestAsk() const { return asks_.empty() ? 0 : asks_.begin()->first; }

bool OrderBook::checkInvariants(std::string* why) const {
    auto fail = [&](const std::string& message) {
        if (why != nullptr) *why = symbol_ + ": " + message;
        return false;
    };

    std::size_t counted = 0;
    auto checkSide = [&](const auto& bookSide, Side side) -> bool {
        for (const auto& [price, queue] : bookSide) {
            if (queue.empty()) return fail("empty price level kept in the book");
            // Time priority: inside one level the orders must be in arrival order (uses Order::operator<).
            if (!std::is_sorted(queue.begin(), queue.end(),
                                [](const std::unique_ptr<Order>& a, const std::unique_ptr<Order>& b) { return *a < *b; }))
                return fail("orders at one price are not in arrival order");
            for (const auto& order : queue) {
                if (order->side() != side) return fail("order on the wrong side");
                if (order->limitPrice() != price) return fail("order stored at the wrong price");
                if (order->quantity() <= 0) return fail("filled order left in the book");
                ++counted;
            }
        }
        return true;
    };

    if (!checkSide(bids_, Side::Buy)) return false;
    if (!checkSide(asks_, Side::Sell)) return false;
    if (counted != index_.size()) return fail("index and book disagree");
    if (!bids_.empty() && !asks_.empty() && bids_.begin()->first >= asks_.begin()->first)
        return fail("book is crossed (best bid >= best ask)");
    return true;
}

std::ostream& operator<<(std::ostream& os, const OrderBook& book) {
    const BookSnapshot snap = book.snapshot(3);
    os << "Order book " << book.symbol_ << "\n";
    os << "  asks:";
    for (auto it = snap.asks.rbegin(); it != snap.asks.rend(); ++it)
        os << "  " << it->quantity << " @ " << formatPrice(it->price);
    os << "\n  bids:";
    for (const auto& level : snap.bids) os << "  " << level.quantity << " @ " << formatPrice(level.price);
    return os << '\n';
}
