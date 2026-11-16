#include "MarketState.h"

void MarketState::addSymbol(const std::string& symbol, Paise initialReference) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (primary_.empty()) primary_ = symbol;  // the first stock decides which replay date is shown
    SymbolState& state = symbols_[symbol];
    state.reference = initialReference;
    state.open = initialReference;
    state.last = 0;
}

bool MarketState::hasSymbol(const std::string& symbol) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return symbols_.count(symbol) > 0;
}

std::vector<std::string> MarketState::symbols() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> names;
    for (const auto& entry : symbols_) names.push_back(entry.first);
    return names;
}

void MarketState::setReference(const std::string& symbol, Paise price, const std::string& dateLabel) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto found = symbols_.find(symbol);
    if (found == symbols_.end()) return;
    found->second.reference = price;
    if (!dateLabel.empty() && symbol == primary_) dateLabel_ = dateLabel;
}

Paise MarketState::reference(const std::string& symbol) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto found = symbols_.find(symbol);
    return found == symbols_.end() ? 0 : found->second.reference;
}

void MarketState::recordTrade(const std::string& symbol, Paise price, int quantity) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto found = symbols_.find(symbol);
    if (found == symbols_.end()) return;
    found->second.last = price;
    found->second.volume += quantity;
    found->second.trades += 1;
}

Paise MarketState::lastTrade(const std::string& symbol) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto found = symbols_.find(symbol);
    if (found == symbols_.end()) return 0;
    return found->second.last > 0 ? found->second.last : found->second.reference;
}

long MarketState::volume(const std::string& symbol) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto found = symbols_.find(symbol);
    return found == symbols_.end() ? 0 : found->second.volume;
}

long MarketState::tradeCount(const std::string& symbol) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto found = symbols_.find(symbol);
    return found == symbols_.end() ? 0 : found->second.trades;
}

Paise MarketState::openPrice(const std::string& symbol) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto found = symbols_.find(symbol);
    return found == symbols_.end() ? 0 : found->second.open;
}

std::map<std::string, Paise> MarketState::marks() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::map<std::string, Paise> result;
    for (const auto& [symbol, state] : symbols_) result[symbol] = state.last > 0 ? state.last : state.reference;
    return result;
}

std::string MarketState::dateLabel() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return dateLabel_;
}
