#include "RiskManager.h"

#include "Errors.h"
#include "Utils.h"

RiskManager::RiskManager(const MarketState& market, int bandPercent) : market_(market), bandPercent_(bandPercent) {}

Portfolio& RiskManager::addTrader(const std::string& traderId, Paise startingCash) {
    auto& slot = accounts_[traderId];
    slot = std::make_unique<Portfolio>(traderId, startingCash);
    return *slot;
}

Portfolio* RiskManager::find(const std::string& traderId) const {
    auto found = accounts_.find(traderId);
    return found == accounts_.end() ? nullptr : found->second.get();
}

std::vector<Portfolio*> RiskManager::portfolios() const {
    std::vector<Portfolio*> list;
    for (const auto& entry : accounts_) list.push_back(entry.second.get());
    return list;
}

void RiskManager::check(const Order& order) {
    Portfolio* account = find(order.traderId());
    if (account == nullptr) throw RiskError("unknown trader " + order.traderId());

    const Paise reference = market_.reference(order.symbol());
    if (reference > 0) {
        const Paise low = reference * (100 - bandPercent_) / 100;
        const Paise high = reference * (100 + bandPercent_) / 100;
        const Paise price = order.limitPrice();
        if (price < low || price > high)
            throw RiskError("price " + formatPrice(price) + " is outside the +-" + std::to_string(bandPercent_) +
                            "% band (" + formatPrice(low) + " to " + formatPrice(high) + ")");
    }
    account->reserve(order);
}

void RiskManager::release(const Order& order) {
    Portfolio* account = find(order.traderId());
    if (account != nullptr) account->release(order);
}
