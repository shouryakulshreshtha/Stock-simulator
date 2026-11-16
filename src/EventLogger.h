// EventLogger.h - writes every order event and every trade to events.csv (the audit trail).
// File handling: std::ofstream is opened in the constructor and closed by the destructor (RAII).
#pragma once
#include <fstream>
#include <mutex>
#include <string>

#include "TradeListener.h"

class EventLogger : public TradeListener {
public:
    explicit EventLogger(const std::string& path);  // throws DataFileError if the file cannot be created
    ~EventLogger() override;

    void onTrade(const Trade& trade) override;
    void onOrderEvent(const OrderEvent& event) override;

private:
    std::ofstream file_;
    std::mutex mutex_;
};
