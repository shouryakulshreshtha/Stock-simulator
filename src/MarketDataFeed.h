// MarketDataFeed.h - where prices come from.
//
//   PriceSource (abstract)
//     |-- CsvPriceSource        reads a Kaggle CSV (e.g. NIFTY-50 daily prices)
//     '-- GeneratedPriceSource  random walk, used when no CSV file is available
//
// MarketDataFeed runs a thread that replays one price row after another. Each CSV row (one trading day)
// is spread over several steps so the price moves smoothly and the market looks live.
#pragma once
#include <atomic>
#include <cstddef>
#include <iosfwd>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "MarketState.h"

struct PriceTick {
    std::string date;
    double open{0};
    double high{0};
    double low{0};
    double close{0};
    long volume{0};

    // Overloaded >>: reads one line  "date,open,high,low,close,volume"  and sets failbit if it is bad.
    friend std::istream& operator>>(std::istream& in, PriceTick& tick);
};

class PriceSource {
public:
    virtual ~PriceSource() = default;
    virtual bool next(PriceTick& out) = 0;  // false when there is no more data
    virtual void rewind() = 0;
    virtual std::string describe() const = 0;
};

class CsvPriceSource : public PriceSource {
public:
    explicit CsvPriceSource(const std::string& path);  // throws DataFileError if the file is unusable

    bool next(PriceTick& out) override;
    void rewind() override { position_ = 0; }
    std::string describe() const override;

    std::size_t rows() const { return ticks_.size(); }
    std::size_t skipped() const { return skipped_; }

private:
    std::string path_;
    std::vector<PriceTick> ticks_;
    std::size_t position_{0};
    std::size_t skipped_{0};
};

class GeneratedPriceSource : public PriceSource {
public:
    GeneratedPriceSource(double startPrice, unsigned seed);

    bool next(PriceTick& out) override;
    void rewind() override {}
    std::string describe() const override { return "generated random walk (no CSV used)"; }

private:
    double price_;
    int day_{0};
    std::mt19937 rng_;
};

class MarketDataFeed {
public:
    MarketDataFeed(MarketState& market, int stepMillis, int stepsPerDay);
    ~MarketDataFeed();  // stops and joins the thread (RAII)
    MarketDataFeed(const MarketDataFeed&) = delete;
    MarketDataFeed& operator=(const MarketDataFeed&) = delete;

    void addSource(const std::string& symbol, std::unique_ptr<PriceSource> source);
    void prime();  // read the first row of every source so every symbol has a price before trading starts
    void start();
    void stop();

    std::vector<std::string> sourceSummary() const;  // "RELIANCE: CSV data/RELIANCE.csv (4953 rows)" ...

private:
    struct Track {
        std::string symbol;
        std::unique_ptr<PriceSource> source;
        PriceTick tick;
        double previousClose{0};
        int step{0};
        std::mt19937 rng;
    };

    void loop();
    void advance(Track& track);

    MarketState& market_;
    int stepMillis_;
    int stepsPerDay_;
    std::vector<Track> tracks_;
    std::atomic<bool> running_{false};
    std::thread thread_;
};
