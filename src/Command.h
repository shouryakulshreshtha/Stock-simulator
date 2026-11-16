// Command.h - what a trader sends to the matching engine: submit a new order, or cancel an old one.
#pragma once
#include <memory>
#include <string>
#include <utility>

#include "Order.h"

struct Command {
    enum class Type { Submit, Cancel };

    Type type{Type::Submit};
    std::unique_ptr<Order> order;  // used by Submit
    OrderId cancelId{0};           // used by Cancel
    std::string symbol;            // used by Cancel
    std::string trader;            // who asks (only the owner may cancel an order)

    static Command submit(std::unique_ptr<Order> newOrder) {
        Command c;
        c.type = Type::Submit;
        c.trader = newOrder ? newOrder->traderId() : std::string();
        c.order = std::move(newOrder);
        return c;
    }

    static Command cancel(const std::string& trader, const std::string& symbol, OrderId id) {
        Command c;
        c.type = Type::Cancel;
        c.trader = trader;
        c.symbol = symbol;
        c.cancelId = id;
        return c;
    }
};
