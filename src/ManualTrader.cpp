#include "ManualTrader.h"

#include <iostream>
#include <sstream>

#include "Errors.h"
#include "Utils.h"

ManualTrader::ManualTrader(std::string id, std::string name)
    : Participant(id, name), Trader(id, name, 0, 0) {}

std::vector<Command> ManualTrader::decide(const MarketState&, std::mt19937&) { return {}; }

std::string ManualTrader::status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

void ManualTrader::setStatus(const std::string& text) {
    std::lock_guard<std::mutex> lock(mutex_);
    status_ = text;
}

void ManualTrader::run(ThreadSafeQueue<Command>& queue, const MarketState& market) {
    std::string line;
    while (running_ && std::getline(std::cin, line)) {
        handleLine(line, queue, market);
        if (promptRow_ > 0) {  // Enter moved the cursor down: put it back on the prompt line
            std::lock_guard<std::mutex> lock(consoleMutex());
            std::cout << "\033[" << promptRow_ << ";1H\033[K> " << std::flush;
        }
    }
}

void ManualTrader::handleLine(const std::string& line, ThreadSafeQueue<Command>& queue, const MarketState& market) {
    std::istringstream in(toUpper(line));
    std::string word;
    if (!(in >> word)) return;  // empty line

    if (word == "QUIT" || word == "EXIT") {
        quit_ = true;
        setStatus("quitting...");
        return;
    }

    try {
        if (word == "BUY" || word == "SELL") {
            const Side side = (word == "BUY") ? Side::Buy : Side::Sell;
            std::string symbol, priceText;
            int quantity = 0;
            if (!(in >> symbol >> quantity >> priceText)) throw InvalidOrderError("use: BUY RELIANCE 10 2450   (or MKT as the price)");
            if (!market.hasSymbol(symbol)) throw InvalidOrderError("unknown symbol " + symbol);

            std::unique_ptr<Order> order;
            if (priceText == "MKT" || priceText == "MARKET") {
                const double reference = toRupees(market.reference(symbol));
                const Paise protection = toPaise(reference * (side == Side::Buy ? 1.05 : 0.95));
                order = std::make_unique<MarketOrder>(symbol, side, quantity, protection, id());
            } else {
                std::size_t used = 0;
                const double price = std::stod(priceText, &used);
                if (used != priceText.size()) throw InvalidOrderError("bad price " + priceText);
                order = std::make_unique<LimitOrder>(symbol, side, quantity, toPaise(price), id());
            }
            const OrderId newId = order->id();
            queue.push(Command::submit(std::move(order)));
            setStatus("sent " + std::string(toString(side)) + " " + symbol + " as order #" + std::to_string(newId));
        } else if (word == "CANCEL") {
            std::string symbol;
            OrderId target = 0;
            if (!(in >> symbol >> target)) throw InvalidOrderError("use: CANCEL RELIANCE 1234");
            queue.push(Command::cancel(id(), symbol, target));
            setStatus("cancel requested for #" + std::to_string(target));
        } else {
            setStatus("unknown command '" + word + "'. Try: BUY RELIANCE 10 2450");
        }
    } catch (const StockSimError& error) {
        setStatus(std::string("rejected: ") + error.what());
    } catch (const std::exception&) {
        setStatus("could not read that number");
    }
}
