// TradeListener.h - the Observer pattern.
// The matching engine knows nothing about screens, files or charts. It only tells every registered
// TradeListener what happened. Dashboard, EventLogger, ChartExporter and Portfolio all implement it.
#pragma once
#include <string>

#include "Trade.h"
#include "Types.h"

// Something that happened to an order (submitted, resting, cancelled, rejected ...).
struct OrderEvent {
    std::string type;
    std::string symbol;
    OrderId orderId{0};
    std::string trader;
    std::string side;
    int quantity{0};
    double price{0.0};
    std::string detail;
};

class TradeListener {
public:
    virtual ~TradeListener() = default;
    virtual void onTrade(const Trade& trade) = 0;             // every listener must handle trades
    virtual void onOrderEvent(const OrderEvent& /*event*/) {}  // optional: most listeners ignore these
};
