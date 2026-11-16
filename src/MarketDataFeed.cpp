#include "MarketDataFeed.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <istream>
#include <sstream>

#include "Errors.h"
#include "Utils.h"

// ---------------------------------------------------------------- helpers

namespace {

std::string trim(const std::string& text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
    while (end > begin && (std::isspace(static_cast<unsigned char>(text[end - 1])) || text[end - 1] == '"')) --end;
    if (begin < end && text[begin] == '"') ++begin;
    return text.substr(begin, end - begin);
}

std::vector<std::string> splitCsvLine(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    std::istringstream stream(line);
    while (std::getline(stream, field, ',')) fields.push_back(trim(field));
    if (!line.empty() && line.back() == ',') fields.emplace_back();
    return fields;
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Position of the first header that matches one of the wanted names, or -1.
int findColumn(const std::vector<std::string>& header, const std::vector<std::string>& wanted) {
    for (const std::string& name : wanted)
        for (std::size_t i = 0; i < header.size(); ++i)
            if (lower(header[i]) == name) return static_cast<int>(i);
    return -1;
}

}  // namespace

// ---------------------------------------------------------------- PriceTick >>

std::istream& operator>>(std::istream& in, PriceTick& tick) {
    std::string line;
    if (!std::getline(in, line)) return in;

    const std::vector<std::string> f = splitCsvLine(line);
    try {
        if (f.size() != 6) throw std::invalid_argument("expected 6 fields");
        PriceTick parsed;
        parsed.date = f[0];
        parsed.open = std::stod(f[1]);
        parsed.high = std::stod(f[2]);
        parsed.low = std::stod(f[3]);
        parsed.close = std::stod(f[4]);
        parsed.volume = f[5].empty() ? 0 : static_cast<long>(std::stod(f[5]));
        tick = parsed;
    } catch (const std::exception&) {
        in.setstate(std::ios::failbit);  // the caller sees a normal stream failure
    }
    return in;
}

// ---------------------------------------------------------------- CsvPriceSource

CsvPriceSource::CsvPriceSource(const std::string& path) : path_(path) {
    std::ifstream file(path);
    if (!file) throw DataFileError("cannot open " + path);

    std::string headerLine;
    if (!std::getline(file, headerLine)) throw DataFileError(path + " is empty");
    const std::vector<std::string> header = splitCsvLine(headerLine);

    // Columns are found by NAME, so the order of the columns in the Kaggle file does not matter.
    const int dateCol = findColumn(header, {"date", "datetime", "timestamp"});
    const int closeCol = findColumn(header, {"close", "close price", "adj close", "last"});
    const int openCol = findColumn(header, {"open", "open price"});
    const int highCol = findColumn(header, {"high", "high price"});
    const int lowCol = findColumn(header, {"low", "low price"});
    const int volumeCol = findColumn(header, {"volume", "total traded quantity"});
    if (dateCol < 0) throw DataFileError(path + " has no Date column");
    if (closeCol < 0) throw DataFileError(path + " has no Close column");

    auto field = [](const std::vector<std::string>& row, int col, const std::string& fallback) {
        return (col >= 0 && static_cast<std::size_t>(col) < row.size()) ? row[static_cast<std::size_t>(col)] : fallback;
    };

    std::string line;
    while (std::getline(file, line)) {
        if (trim(line).empty()) continue;
        const std::vector<std::string> row = splitCsvLine(line);
        const std::string close = field(row, closeCol, "");

        // Rebuild the row in a fixed order and let operator>> parse it.
        std::ostringstream normal;
        normal << field(row, dateCol, "") << ',' << field(row, openCol, close) << ',' << field(row, highCol, close) << ','
               << field(row, lowCol, close) << ',' << close << ',' << field(row, volumeCol, "0");
        std::istringstream reader(normal.str());
        PriceTick tick;
        if (!(reader >> tick) || tick.close <= 0.0 || tick.date.empty()) {
            ++skipped_;  // empty Close, text instead of a number, negative price ...
            continue;
        }
        tick.high = std::max({tick.high, tick.open, tick.close});
        tick.low = std::min({tick.low > 0 ? tick.low : tick.close, tick.open > 0 ? tick.open : tick.close, tick.close});
        ticks_.push_back(tick);
    }
    if (ticks_.empty()) throw DataFileError(path + " has no usable price rows");
}

bool CsvPriceSource::next(PriceTick& out) {
    if (position_ >= ticks_.size()) return false;
    out = ticks_[position_++];
    return true;
}

std::string CsvPriceSource::describe() const {
    std::ostringstream os;
    os << "CSV " << path_ << " (" << ticks_.size() << " rows";
    if (skipped_ > 0) os << ", " << skipped_ << " bad rows skipped";
    os << ')';
    return os.str();
}

// ---------------------------------------------------------------- GeneratedPriceSource

GeneratedPriceSource::GeneratedPriceSource(double startPrice, unsigned seed) : price_(startPrice), rng_(seed) {}

bool GeneratedPriceSource::next(PriceTick& out) {
    std::normal_distribution<double> dailyReturn(0.0003, 0.011);
    std::normal_distribution<double> wick(0.0, 0.004);
    const double open = price_;
    const double close = clampValue(price_ * (1.0 + dailyReturn(rng_)), open * 0.93, open * 1.07);
    out.date = "SIM-" + std::to_string(++day_);
    out.open = open;
    out.close = close;
    out.high = std::max(open, close) * (1.0 + std::fabs(wick(rng_)));
    out.low = std::min(open, close) * (1.0 - std::fabs(wick(rng_)));
    out.volume = 1000000;
    price_ = close;
    return true;  // never runs out
}

// ---------------------------------------------------------------- MarketDataFeed

MarketDataFeed::MarketDataFeed(MarketState& market, int stepMillis, int stepsPerDay)
    : market_(market), stepMillis_(stepMillis), stepsPerDay_(std::max(1, stepsPerDay)) {}

MarketDataFeed::~MarketDataFeed() { stop(); }

void MarketDataFeed::addSource(const std::string& symbol, std::unique_ptr<PriceSource> source) {
    Track track;
    track.symbol = symbol;
    track.source = std::move(source);
    track.rng.seed(static_cast<unsigned>(std::hash<std::string>{}(symbol)));
    tracks_.push_back(std::move(track));
}

void MarketDataFeed::prime() {
    for (Track& track : tracks_) {
        if (!track.source->next(track.tick)) {
            track.source->rewind();
            if (!track.source->next(track.tick)) throw DataFileError("no data for " + track.symbol);
        }
        track.previousClose = track.tick.close;
        track.step = 0;
        market_.addSymbol(track.symbol, toPaise(track.tick.close));
        market_.setReference(track.symbol, toPaise(track.tick.close), track.tick.date);
    }
}

// One step: move the reference price a little closer to the close of the current trading day.
void MarketDataFeed::advance(Track& track) {
    if (track.step == 0) {
        track.previousClose = track.tick.close;
        if (!track.source->next(track.tick)) {  // end of the data: start again from the first row
            track.source->rewind();
            track.source->next(track.tick);
        }
    }
    ++track.step;
    const double progress = static_cast<double>(track.step) / stepsPerDay_;
    double price = track.previousClose + (track.tick.close - track.previousClose) * progress;

    if (track.step < stepsPerDay_) {  // some noise inside the day's high-low range, except at the close
        const double range = std::max(0.0, track.tick.high - track.tick.low);
        std::normal_distribution<double> noise(0.0, range * 0.12);
        price += noise(track.rng);
        if (track.tick.high > track.tick.low) price = clampValue(price, track.tick.low, track.tick.high);
    } else {
        price = track.tick.close;
        track.step = 0;
    }
    market_.setReference(track.symbol, toPaise(price), track.tick.date);
}

void MarketDataFeed::start() {
    if (running_.exchange(true)) return;
    thread_ = std::thread([this] { loop(); });
}

void MarketDataFeed::loop() {
    while (running_) {
        for (Track& track : tracks_) advance(track);
        // Sleep in small pieces so stop() does not have to wait for a whole step.
        for (int waited = 0; waited < stepMillis_ && running_; waited += 20)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

void MarketDataFeed::stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

std::vector<std::string> MarketDataFeed::sourceSummary() const {
    std::vector<std::string> lines;
    for (const Track& track : tracks_) lines.push_back(track.symbol + ": " + track.source->describe());
    return lines;
}
