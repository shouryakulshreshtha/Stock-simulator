// MarketState.h - prices everybody can read: the replayed reference price and the last traded price.
// The data feed thread writes the reference price, the engine writes trades, bots and the dashboard read.
#pragma once
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "Types.h"

class MarketState {
public:
    void addSymbol(const std::string& symbol, Paise initialReference);
    bool hasSymbol(const std::string& symbol) const;
    std::vector<std::string> symbols() const;

    void setReference(const std::string& symbol, Paise price, const std::string& dateLabel);
    Paise reference(const std::string& symbol) const;  // 0 when the symbol is unknown

    void recordTrade(const std::string& symbol, Paise price, int quantity);
    Paise lastTrade(const std::string& symbol) const;  // falls back to the reference price
    long volume(const std::string& symbol) const;
    long tradeCount(const std::string& symbol) const;
    Paise openPrice(const std::string& symbol) const;  // first reference price seen

    std::map<std::string, Paise> marks() const;  // last trade (or reference) per symbol, for P&L
    std::string dateLabel() const;               // date of the CSV row being replayed (first stock)

private:
    struct SymbolState {
        Paise reference{0};
        Paise open{0};
        Paise last{0};
        long volume{0};
        long trades{0};
    };

    mutable std::mutex mutex_;
    std::map<std::string, SymbolState> symbols_;
    std::string dateLabel_;
    std::string primary_;  // first symbol added
};
