#include "ChartExporter.h"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

#include "Utils.h"

ChartExporter::ChartExporter(const MatchingEngine& engine, const MarketState& market, std::string path, int bucketSeconds,
                             std::size_t maxBuckets)
    : engine_(engine),
      market_(market),
      path_(std::move(path)),
      bucketSeconds_(bucketSeconds),
      maxBuckets_(maxBuckets),
      start_(std::chrono::steady_clock::now()),
      lastFlush_(start_) {}

long ChartExporter::currentIndex() const {
    const auto elapsed = std::chrono::steady_clock::now() - start_;
    return static_cast<long>(std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() / bucketSeconds_);
}

// Make sure there is a bucket for every time slot up to nowIndex (quiet slots repeat the last price).
void ChartExporter::roll(const std::string& symbol, Series& series, long nowIndex) {
    if (series.lastPrice == 0) series.lastPrice = market_.reference(symbol);
    while (series.index < nowIndex) {
        Bucket bucket;
        bucket.label = timeStamp();
        bucket.open = bucket.high = bucket.low = bucket.close = series.lastPrice;
        bucket.reference = market_.reference(symbol);
        series.buckets.push_back(bucket);
        ++series.index;
        if (series.buckets.size() > maxBuckets_) series.buckets.pop_front();
    }
}

void ChartExporter::onTrade(const Trade& trade) {
    std::lock_guard<std::mutex> lock(mutex_);
    Series& series = series_[trade.symbol()];
    roll(trade.symbol(), series, currentIndex());

    Bucket& bucket = series.buckets.back();
    const Paise price = trade.price();
    if (!bucket.traded) {
        bucket.open = bucket.high = bucket.low = price;
        bucket.traded = true;
    }
    bucket.high = std::max(bucket.high, price);
    bucket.low = std::min(bucket.low, price);
    bucket.close = price;
    bucket.volume += trade.quantity();
    series.lastPrice = price;
}

void ChartExporter::flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    const long now = currentIndex();
    for (const std::string& symbol : market_.symbols()) {
        Series& series = series_[symbol];
        roll(symbol, series, now);
        if (!series.buckets.empty()) series.buckets.back().reference = market_.reference(symbol);
    }

    // Orders per second since the last flush.
    const auto clock = std::chrono::steady_clock::now();
    const double seconds = std::chrono::duration<double>(clock - lastFlush_).count();
    const MatchingEngine::Stats stats = engine_.stats();
    if (seconds > 0.5) {
        ordersPerSecond_ = static_cast<double>(stats.orders - lastOrders_) / seconds;
        lastOrders_ = stats.orders;
        lastFlush_ = clock;
    }

    std::ostringstream js;
    js << std::fixed << std::setprecision(2);
    js << "window.STOCKSIM = {\n";
    js << "  updated: \"" << timeStamp() << "\", replay: \"" << market_.dateLabel() << "\",\n";
    js << "  stats: { orders: " << stats.orders << ", trades: " << stats.trades << ", rejected: " << stats.rejected
       << ", cancels: " << stats.cancels << ", ordersPerSec: " << ordersPerSecond_ << " },\n";
    js << "  symbols: {\n";
    bool firstSymbol = true;
    for (const std::string& symbol : market_.symbols()) {
        const Series& series = series_[symbol];
        if (!firstSymbol) js << ",\n";
        firstSymbol = false;
        js << "    \"" << symbol << "\": {\n";
        js << "      last: " << toRupees(market_.lastTrade(symbol)) << ", ref: " << toRupees(market_.reference(symbol))
           << ", open: " << toRupees(market_.openPrice(symbol)) << ", volume: " << market_.volume(symbol)
           << ", trades: " << market_.tradeCount(symbol) << ",\n";

        auto writeArray = [&](const char* name, auto valueOf) {
            js << "      " << name << ": [";
            bool first = true;
            for (const Bucket& b : series.buckets) {
                if (!first) js << ',';
                first = false;
                valueOf(b);
            }
            js << "],\n";
        };
        writeArray("labels", [&](const Bucket& b) { js << '"' << b.label << '"'; });
        writeArray("price", [&](const Bucket& b) { js << toRupees(b.close); });
        writeArray("reference", [&](const Bucket& b) { js << toRupees(b.reference); });
        js << "      vol: [";
        bool first = true;
        for (const Bucket& b : series.buckets) {
            if (!first) js << ',';
            first = false;
            js << b.volume;
        }
        js << "]\n    }";
    }
    js << "\n  }\n};\n";

    // Write to a temporary file and rename it, so the browser never reads a half-written file.
    try {
        const std::filesystem::path target(path_);
        if (target.has_parent_path()) std::filesystem::create_directories(target.parent_path());
        const std::string temporary = path_ + ".tmp";
        {
            std::ofstream out(temporary);
            out << js.str();
        }
        std::filesystem::rename(temporary, target);
    } catch (const std::exception&) {
        // The chart is optional: a locked file must never stop the exchange.
    }
}
