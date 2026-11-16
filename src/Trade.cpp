#include "Trade.h"

#include <ctime>
#include <iomanip>
#include <ostream>
#include <sstream>

#include "Utils.h"

Trade::Trade(std::string symbol, std::uint64_t sequence, OrderId buyOrderId, OrderId sellOrderId, std::string buyer,
             std::string seller, int quantity, Paise price)
    : symbol_(std::move(symbol)),
      sequence_(sequence),
      buyOrderId_(buyOrderId),
      sellOrderId_(sellOrderId),
      buyer_(std::move(buyer)),
      seller_(std::move(seller)),
      quantity_(quantity),
      price_(price),
      time_(std::chrono::system_clock::now()) {}

std::string Trade::timeString() const {
    std::time_t t = std::chrono::system_clock::to_time_t(time_);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif
    std::ostringstream os;
    os << std::put_time(&local, "%H:%M:%S");
    return os.str();
}

std::ostream& operator<<(std::ostream& os, const Trade& trade) {
    return os << trade.symbol_ << ' ' << trade.quantity_ << " @ " << formatPrice(trade.price_) << "  (buy #"
              << trade.buyOrderId_ << ' ' << trade.buyer_ << " / sell #" << trade.sellOrderId_ << ' ' << trade.seller_
              << ')';
}
