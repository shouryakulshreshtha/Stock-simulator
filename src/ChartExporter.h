// ChartExporter.h - feeds the graphical chart.
// The C++ program cannot draw a smooth chart without a GUI library, so it writes the numbers to
// web/chart_data.js and web/chart.html (opened in any browser) reloads that file every second and draws them.
// Trades are grouped into small time buckets (open, high, low, close, volume) per stock.
#pragma once
#include <chrono>
#include <deque>
#include <map>
#include <mutex>
#include <string>

#include "MarketState.h"
#include "MatchingEngine.h"
#include "TradeListener.h"

class ChartExporter : public TradeListener {
public:
    ChartExporter(const MatchingEngine& engine, const MarketState& market, std::string path, int bucketSeconds = 2,
                  std::size_t maxBuckets = 240);

    void onTrade(const Trade& trade) override;
    void flush();  // write the data file now (called about once per second by the main loop)

private:
    struct Bucket {
        std::string label;
        Paise open{0}, high{0}, low{0}, close{0};
        long volume{0};
        bool traded{false};
        Paise reference{0};
    };
    struct Series {
        std::deque<Bucket> buckets;
        long index{-1};
        Paise lastPrice{0};
    };

    void roll(const std::string& symbol, Series& series, long nowIndex);
    long currentIndex() const;

    const MatchingEngine& engine_;
    const MarketState& market_;
    std::string path_;
    int bucketSeconds_;
    std::size_t maxBuckets_;
    std::chrono::steady_clock::time_point start_;
    std::chrono::steady_clock::time_point lastFlush_;
    long lastOrders_{0};
    double ordersPerSecond_{0.0};

    std::map<std::string, Series> series_;
    std::mutex mutex_;
};
