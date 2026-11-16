#include "Bots.h"

#include "Errors.h"

// ---------------------------------------------------------------- BotTrader

// The most derived class (RandomBot ...) initialises the virtual base Participant, so the
// Participant(...) below is only used if a BotTrader were ever created on its own.
BotTrader::BotTrader(std::string id, std::string name, int intervalMillis, unsigned seed, std::vector<std::string> symbols)
    : Participant(id, name),
      Trader(id, name, intervalMillis, seed),
      Auditable(id, name),
      symbols_(std::move(symbols)) {}

const std::string& BotTrader::pickSymbol(std::mt19937& rng) const {
    std::uniform_int_distribution<std::size_t> pick(0, symbols_.size() - 1);
    return symbols_[pick(rng)];
}

Paise BotTrader::around(Paise reference, double fraction) {
    return toPaise(toRupees(reference) * (1.0 + fraction));
}

Command BotTrader::limitOrder(const std::string& symbol, Side side, int quantity, Paise price) {
    noteSent();
    return Command::submit(std::make_unique<LimitOrder>(symbol, side, quantity, price, id()));
}

Command BotTrader::marketOrder(const std::string& symbol, Side side, int quantity, Paise protectionPrice) {
    noteSent();
    return Command::submit(std::make_unique<MarketOrder>(symbol, side, quantity, protectionPrice, id()));
}

Command BotTrader::cancelOrder(const std::string& symbol, OrderId id) {
    noteSent();
    return Command::cancel(this->id(), symbol, id);
}

// ---------------------------------------------------------------- RandomBot

RandomBot::RandomBot(std::string id, int intervalMillis, unsigned seed, std::vector<std::string> symbols)
    : Participant(id, id), BotTrader(id, id, intervalMillis, seed, std::move(symbols)) {}

std::vector<Command> RandomBot::decide(const MarketState& market, std::mt19937& rng) {
    std::vector<Command> commands;
    std::uniform_real_distribution<double> chance(0.0, 1.0);

    // Now and then cancel one of our own older orders.
    if (!myOrders_.empty() && chance(rng) < 0.06) {
        std::uniform_int_distribution<std::size_t> pick(0, myOrders_.size() - 1);
        const std::size_t index = pick(rng);
        commands.push_back(cancelOrder(myOrders_[index].first, myOrders_[index].second));
        myOrders_.erase(myOrders_.begin() + static_cast<long>(index));
        return commands;
    }

    const std::string& symbol = pickSymbol(rng);
    const Paise reference = market.reference(symbol);
    if (reference <= 0) return commands;

    const Side side = chance(rng) < 0.5 ? Side::Buy : Side::Sell;
    std::uniform_int_distribution<int> quantityPick(1, 30);
    const int quantity = clampValue(quantityPick(rng), 1, 30);

    if (chance(rng) < 0.15) {
        const Paise protection = around(reference, side == Side::Buy ? 0.03 : -0.03);
        commands.push_back(marketOrder(symbol, side, quantity, protection));
    } else {
        std::uniform_real_distribution<double> offset(-0.004, 0.004);  // within 0.4% of the reference price
        Command command = limitOrder(symbol, side, quantity, around(reference, offset(rng)));
        myOrders_.emplace_back(symbol, command.order->id());
        if (myOrders_.size() > 40) myOrders_.erase(myOrders_.begin());
        commands.push_back(std::move(command));
    }
    return commands;
}

// ---------------------------------------------------------------- MomentumBot

MomentumBot::MomentumBot(std::string id, int intervalMillis, unsigned seed, std::vector<std::string> symbols)
    : Participant(id, id), BotTrader(id, id, intervalMillis, seed, std::move(symbols)) {}

std::vector<Command> MomentumBot::decide(const MarketState& market, std::mt19937& rng) {
    std::vector<Command> commands;
    std::uniform_int_distribution<int> quantityPick(5, 40);

    for (const std::string& symbol : symbols_) {
        const Paise reference = market.reference(symbol);
        if (reference <= 0) continue;

        auto found = averages_.find(symbol);
        if (found == averages_.end()) found = averages_.emplace(symbol, RollingAverage<Paise>(10)).first;
        RollingAverage<Paise>& average = found->second;
        average.add(reference);
        if (!average.ready() || !commands.empty()) continue;  // one trade per round is enough

        const double trend = static_cast<double>(reference) / average.average() - 1.0;
        if (trend > 0.0015)
            commands.push_back(marketOrder(symbol, Side::Buy, quantityPick(rng), around(reference, 0.02)));
        else if (trend < -0.0015)
            commands.push_back(marketOrder(symbol, Side::Sell, quantityPick(rng), around(reference, -0.02)));
    }
    return commands;
}

// ---------------------------------------------------------------- MarketMakerBot

MarketMakerBot::MarketMakerBot(std::string id, int intervalMillis, unsigned seed, std::vector<std::string> symbols)
    : Participant(id, id), BotTrader(id, id, intervalMillis, seed, std::move(symbols)) {}

std::vector<Command> MarketMakerBot::decide(const MarketState& market, std::mt19937& rng) {
    std::vector<Command> commands;
    std::uniform_int_distribution<int> quantityPick(20, 60);

    for (const std::string& symbol : symbols_) {
        const Paise reference = market.reference(symbol);
        if (reference <= 0) continue;

        auto old = quotes_.find(symbol);  // take the old quotes away, then quote around the new price
        if (old != quotes_.end()) {
            commands.push_back(cancelOrder(symbol, old->second.first));
            commands.push_back(cancelOrder(symbol, old->second.second));
        }
        Command bid = limitOrder(symbol, Side::Buy, quantityPick(rng), around(reference, -0.0012));
        Command ask = limitOrder(symbol, Side::Sell, quantityPick(rng), around(reference, 0.0012));
        quotes_[symbol] = {bid.order->id(), ask.order->id()};
        commands.push_back(std::move(bid));
        commands.push_back(std::move(ask));
    }
    return commands;
}
