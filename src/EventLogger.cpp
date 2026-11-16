#include "EventLogger.h"

#include <iomanip>

#include "Errors.h"
#include "Utils.h"

namespace {
// Keep the CSV valid even if a message contains a comma or a quote.
std::string csvField(const std::string& text) {
    if (text.find_first_of(",\"\n") == std::string::npos) return text;
    std::string quoted = "\"";
    for (char c : text) quoted += (c == '"') ? std::string("\"\"") : std::string(1, c);
    return quoted + "\"";
}
}  // namespace

EventLogger::EventLogger(const std::string& path) : file_(path) {
    if (!file_) throw DataFileError("cannot create log file " + path);
    file_ << "time,event,symbol,order_id,trader,side,quantity,price,detail\n";
}

EventLogger::~EventLogger() {
    if (file_.is_open()) file_.close();
}

void EventLogger::onTrade(const Trade& trade) {
    std::lock_guard<std::mutex> lock(mutex_);
    file_ << trade.timeString() << ",TRADE," << trade.symbol() << ',' << trade.buyOrderId() << ',' << csvField(trade.buyer())
          << ",BUY," << trade.quantity() << ',' << std::fixed << std::setprecision(2) << toRupees(trade.price())
          << ",sold by " << csvField(trade.seller()) << " (order #" << trade.sellOrderId() << ")\n";
    file_.flush();
}

void EventLogger::onOrderEvent(const OrderEvent& event) {
    std::lock_guard<std::mutex> lock(mutex_);
    file_ << timeStamp() << ',' << event.type << ',' << event.symbol << ',' << event.orderId << ',' << csvField(event.trader)
          << ',' << event.side << ',' << event.quantity << ',' << std::fixed << std::setprecision(2) << event.price << ','
          << csvField(event.detail) << '\n';
    file_.flush();
}
