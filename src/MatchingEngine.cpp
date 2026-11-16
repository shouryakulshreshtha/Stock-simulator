#include "MatchingEngine.h"

#include "Errors.h"
#include "Utils.h"

MatchingEngine::MatchingEngine(ThreadSafeQueue<Command>& queue, RiskManager& risk, MarketState& market)
    : queue_(queue), risk_(risk), market_(market) {}

MatchingEngine::~MatchingEngine() { stop(); }

void MatchingEngine::addSymbol(const std::string& symbol) {
    std::lock_guard<std::mutex> lock(bookMutex_);
    books_.emplace(symbol, OrderBook(symbol));
}

void MatchingEngine::addListener(TradeListener* listener) { listeners_.push_back(listener); }

void MatchingEngine::start() {
    if (thread_.joinable()) return;
    thread_ = std::thread([this] {
        Command command;
        while (queue_.pop(command)) process(std::move(command));
    });
}

void MatchingEngine::stop() {
    queue_.close();
    if (thread_.joinable()) thread_.join();
}

OrderEvent MatchingEngine::makeEvent(const std::string& type, const Order& order, const std::string& detail) {
    OrderEvent event;
    event.type = type;
    event.symbol = order.symbol();
    event.orderId = order.id();
    event.trader = order.traderId();
    event.side = toString(order.side());
    event.quantity = order.quantity();
    event.price = toRupees(order.limitPrice());
    event.detail = detail;
    return event;
}

void MatchingEngine::emit(const OrderEvent& event) {
    for (TradeListener* listener : listeners_) listener->onOrderEvent(event);
}

void MatchingEngine::process(Command&& command) {
    std::lock_guard<std::mutex> lock(bookMutex_);
    try {
        if (command.type == Command::Type::Cancel)
            handleCancel(command);
        else
            handleSubmit(std::move(command.order));
    } catch (const StockSimError& error) {
        // Whatever went wrong, one bad command never stops the exchange.
        ++rejected_;
        OrderEvent event;
        event.type = "REJECT";
        event.trader = command.trader;
        event.symbol = command.symbol;
        event.detail = error.what();
        emit(event);
    }
}

void MatchingEngine::handleSubmit(std::unique_ptr<Order> order) {
    if (!order) throw InvalidOrderError("empty order");

    auto bookIt = books_.find(order->symbol());
    if (bookIt == books_.end()) {
        ++rejected_;
        emit(makeEvent("REJECT", *order, "unknown symbol " + order->symbol()));
        return;
    }

    try {
        risk_.check(*order);  // price band + cash/share reservation
    } catch (const StockSimError& error) {
        ++rejected_;
        emit(makeEvent("REJECT", *order, error.what()));
        return;
    }

    ++orders_;
    const std::string symbol = order->symbol();
    OrderEvent restEvent = makeEvent("REST", *order, "waiting in the order book");
    emit(makeEvent("SUBMIT", *order));

    MatchResult result = bookIt->second.submit(std::move(order), ++arrival_);

    for (const Trade& trade : result.trades) {
        ++trades_;
        market_.recordTrade(symbol, trade.price(), trade.quantity());
        for (TradeListener* listener : listeners_) listener->onTrade(trade);  // Observer pattern
    }
    for (const auto& removed : result.cancelled) {
        risk_.release(*removed);
        emit(makeEvent("STP_CANCEL", *removed, "self-trade prevented"));
    }
    if (result.dropped) {
        risk_.release(*result.dropped);
        emit(makeEvent("UNFILLED", *result.dropped, "no more liquidity inside its price limit"));
    } else if (result.rested) {
        emit(restEvent);
    }
}

void MatchingEngine::handleCancel(const Command& command) {
    auto bookIt = books_.find(command.symbol);
    if (bookIt == books_.end()) throw InvalidOrderError("unknown symbol " + command.symbol);

    const Order* existing = bookIt->second.find(command.cancelId);
    OrderEvent event;
    event.symbol = command.symbol;
    event.orderId = command.cancelId;
    event.trader = command.trader;

    if (existing == nullptr) {
        // Normal in a fast market: the order was filled a moment ago. Not an error.
        event.type = "CANCEL_MISS";
        event.detail = "already filled or cancelled";
        emit(event);
        return;
    }
    if (existing->traderId() != command.trader) throw RiskError(command.trader + " cannot cancel an order of another trader");

    std::unique_ptr<Order> removed = bookIt->second.cancel(command.cancelId);
    risk_.release(*removed);
    ++cancels_;
    emit(makeEvent("CANCEL", *removed, "removed from the book"));
}

void MatchingEngine::submit(std::unique_ptr<LimitOrder> order) { process(Command::submit(std::move(order))); }

void MatchingEngine::submit(std::unique_ptr<MarketOrder> order) { process(Command::submit(std::move(order))); }

void MatchingEngine::cancel(const std::string& trader, const std::string& symbol, OrderId id) {
    process(Command::cancel(trader, symbol, id));
}

BookSnapshot MatchingEngine::snapshot(const std::string& symbol, std::size_t depth) const {
    std::lock_guard<std::mutex> lock(bookMutex_);
    auto found = books_.find(symbol);
    if (found == books_.end()) return BookSnapshot{symbol, {}, {}};
    return found->second.snapshot(depth);
}

MatchingEngine::Stats MatchingEngine::stats() const {
    Stats s;
    s.orders = orders_.load();
    s.trades = trades_.load();
    s.rejected = rejected_.load();
    s.cancels = cancels_.load();
    return s;
}

bool MatchingEngine::checkInvariants(std::string* why) const {
    std::lock_guard<std::mutex> lock(bookMutex_);
    for (const auto& entry : books_)
        if (!entry.second.checkInvariants(why)) return false;
    return true;
}
