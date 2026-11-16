// main.cpp - builds the exchange, starts every thread, runs the live screen, shuts everything down.
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "Bots.h"
#include "ChartExporter.h"
#include "Dashboard.h"
#include "Errors.h"
#include "EventLogger.h"
#include "ManualTrader.h"
#include "MarketDataFeed.h"
#include "MarketState.h"
#include "MatchingEngine.h"
#include "RiskManager.h"
#include "ThreadSafeQueue.h"
#include "Utils.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

std::atomic<bool> g_stop{false};
void onSignal(int) { g_stop = true; }  // Ctrl+C asks the main loop to finish cleanly

struct Options {
    std::string dataDir = "data";
    std::string webDir = "web";
    std::string logPath = "events.csv";
    std::vector<std::string> symbols = {"RELIANCE", "TCS", "HDFCBANK"};
    int stepMillis = 250;     // one replay step
    int stepsPerDay = 20;     // steps per CSV row (trading day)
    int durationSeconds = 0;  // 0 = run until QUIT or Ctrl+C
    bool headless = false;    // no keyboard trader (for tests and screenshots)
    bool ansi = true;         // draw in place with terminal escape codes
    unsigned seed = 42;
};

void printHelp() {
    std::cout << "StockSim - live stock exchange simulator (UNC302 OOP project)\n\n"
                 "  --data DIR         folder with <SYMBOL>.csv price files           (default: data)\n"
                 "  --symbols A,B,C    stocks to trade                                (default: RELIANCE,TCS,HDFCBANK)\n"
                 "  --web DIR          folder where chart_data.js is written          (default: web)\n"
                 "  --log FILE         audit log of every order and trade             (default: events.csv)\n"
                 "  --step MS          milliseconds per replay step                   (default: 250)\n"
                 "  --steps-per-day N  replay steps per CSV row                       (default: 20)\n"
                 "  --duration SEC     stop after SEC seconds                         (default: run until QUIT)\n"
                 "  --headless         no keyboard trader, prints a final report\n"
                 "  --no-ansi          plain scrolling output instead of an in-place screen\n"
                 "  --seed N           random seed                                    (default: 42)\n";
}

Options parseArgs(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= argc) throw std::invalid_argument("missing value after " + arg);
            return argv[++i];
        };
        if (arg == "--data") o.dataDir = value();
        else if (arg == "--web") o.webDir = value();
        else if (arg == "--log") o.logPath = value();
        else if (arg == "--step") o.stepMillis = std::stoi(value());
        else if (arg == "--steps-per-day") o.stepsPerDay = std::stoi(value());
        else if (arg == "--duration") o.durationSeconds = std::stoi(value());
        else if (arg == "--seed") o.seed = static_cast<unsigned>(std::stoul(value()));
        else if (arg == "--headless") o.headless = true;
        else if (arg == "--no-ansi") o.ansi = false;
        else if (arg == "--symbols") {
            o.symbols.clear();
            std::istringstream list(value());
            std::string name;
            while (std::getline(list, name, ',')) if (!name.empty()) o.symbols.push_back(toUpper(name));
        } else if (arg == "--help" || arg == "-h") {
            printHelp();
            std::exit(0);
        } else {
            throw std::invalid_argument("unknown option " + arg + " (try --help)");
        }
    }
    if (o.symbols.empty()) throw std::invalid_argument("no symbols given");
    if (o.stepMillis < 20 || o.stepsPerDay < 1) throw std::invalid_argument("--step must be at least 20 and --steps-per-day at least 1");
    return o;
}

void enableAnsi() {
#ifdef _WIN32
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(out, &mode)) SetConsoleMode(out, mode | 0x0004);  // ENABLE_VIRTUAL_TERMINAL_PROCESSING
#endif
}

double fallbackStartPrice(const std::string& symbol) {
    static const std::map<std::string, double> known = {{"RELIANCE", 2450.0}, {"TCS", 3300.0}, {"HDFCBANK", 1600.0},
                                                        {"INFY", 1500.0},     {"ITC", 450.0},   {"SBIN", 600.0}};
    auto found = known.find(symbol);
    return found == known.end() ? 1000.0 : found->second;
}

// Stops every trader thread BEFORE the traders are destroyed (members are destroyed after the destructor body).
struct TraderPool {
    std::vector<std::unique_ptr<Trader>> traders;
    ~TraderPool() { stopAll(); }
    void stopAll() {
        for (auto& trader : traders) trader->stop();
    }
};

void sleepFor(int millis) {
    for (int waited = 0; waited < millis && !g_stop; waited += 50) std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

int runSimulation(const Options& options) {
    // ---- prices: Kaggle CSV per stock, generated random walk when the file is missing or unusable
    MarketState market;
    MarketDataFeed feed(market, options.stepMillis, options.stepsPerDay);
    std::string dataLine;
    for (const std::string& symbol : options.symbols) {
        const std::string path = options.dataDir + "/" + symbol + ".csv";
        try {
            feed.addSource(symbol, std::make_unique<CsvPriceSource>(path));
            dataLine += symbol + "=csv  ";
        } catch (const DataFileError& error) {  // exception handling: fall back instead of crashing
            std::cout << "note: " << error.what() << " -> using generated prices for " << symbol << "\n";
            feed.addSource(symbol, std::make_unique<GeneratedPriceSource>(fallbackStartPrice(symbol), options.seed));
            dataLine += symbol + "=generated  ";
        }
    }
    feed.prime();
    for (const std::string& line : feed.sourceSummary()) std::cout << "data  " << line << "\n";

    // ---- accounts
    const std::vector<std::string>& symbols = options.symbols;
    RiskManager risk(market);
    struct Account { std::string id; double cash; int shares; };
    std::vector<Account> accounts = {{"Retail-1", 10'000'000, 5000}, {"Retail-2", 10'000'000, 5000},
                                     {"Momentum", 10'000'000, 5000}, {"MarketMaker", 50'000'000, 20000}};
    if (!options.headless) accounts.push_back({"YOU", 1'000'000, 200});
    for (const Account& account : accounts) {
        Portfolio& portfolio = risk.addTrader(account.id, toPaise(account.cash));
        for (const std::string& symbol : symbols) portfolio.addShares(symbol, account.shares);
        portfolio.markStart(market.marks());
    }

    // ---- the exchange
    ThreadSafeQueue<Command> queue;
    EventLogger logger(options.logPath);
    MatchingEngine engine(queue, risk, market);
    for (const std::string& symbol : symbols) engine.addSymbol(symbol);
    const bool anyGenerated = dataLine.find("generated") != std::string::npos;
    Dashboard dashboard(engine, market, risk,
                        anyGenerated ? dataLine + " (put Kaggle CSV files in " + options.dataDir + "/ for real prices)" : dataLine);
    ChartExporter chart(engine, market, options.webDir + "/chart_data.js");

    for (Portfolio* portfolio : risk.portfolios()) engine.addListener(portfolio);
    engine.addListener(&dashboard);
    engine.addListener(&logger);
    engine.addListener(&chart);

    // ---- the traders
    TraderPool pool;
    pool.traders.push_back(std::make_unique<RandomBot>("Retail-1", 300, options.seed + 1, symbols));
    pool.traders.push_back(std::make_unique<RandomBot>("Retail-2", 450, options.seed + 2, symbols));
    pool.traders.push_back(std::make_unique<MomentumBot>("Momentum", 800, options.seed + 3, symbols));
    pool.traders.push_back(std::make_unique<MarketMakerBot>("MarketMaker", 1200, options.seed + 4, symbols));
    ManualTrader* manual = nullptr;
    if (!options.headless) {
        auto trader = std::make_unique<ManualTrader>("YOU", "You (keyboard)");
        manual = trader.get();
        pool.traders.push_back(std::move(trader));
    }
    for (auto& trader : pool.traders)
        if (auto* audited = dynamic_cast<Auditable*>(trader.get())) dashboard.addAuditable(audited);  // cross-cast between the two bases
    if (manual != nullptr) dashboard.setStatusSource([manual] { return manual->status(); });

    const bool inPlace = options.ansi && !options.headless;
    const int promptRow = Dashboard::kFrameLines + 2;
    if (manual != nullptr && inPlace) manual->setPromptRow(promptRow);

    // ---- go
    engine.start();
    feed.start();
    for (auto& trader : pool.traders) trader->start(queue, market);

    if (inPlace) std::cout << "\033[2J\033[H" << std::flush;
    std::cout << "StockSim running. Chart: open " << options.webDir << "/chart.html in a browser.\n" << std::flush;

    const auto began = std::chrono::steady_clock::now();
    long tick = 0;
    while (!g_stop && !(manual != nullptr && manual->quitRequested())) {
        if (options.durationSeconds > 0 &&
            std::chrono::steady_clock::now() - began >= std::chrono::seconds(options.durationSeconds))
            break;
        const std::string frame = dashboard.render(inPlace);
        {
            std::lock_guard<std::mutex> lock(consoleMutex());
            if (!inPlace && options.headless == false) std::cout << frame << std::flush;
            if (inPlace) {
                std::cout << frame;
                if (tick == 0 && manual != nullptr) std::cout << "\033[" << promptRow << ";1H> ";
                std::cout << std::flush;
            }
        }
        if (tick++ % 2 == 0) chart.flush();
        sleepFor(500);
    }

    // ---- shut down in a safe order: traders, price feed, engine
    pool.stopAll();
    feed.stop();
    engine.stop();
    chart.flush();

    if (inPlace) std::cout << "\033[" << promptRow + 1 << ";1H\n";
    std::cout << dashboard.render(false);

    const MatchingEngine::Stats stats = engine.stats();
    std::string why;
    const bool consistent = engine.checkInvariants(&why);
    std::cout << "\n==== final report ====\n"
              << "orders accepted : " << stats.orders << "\n"
              << "trades          : " << stats.trades << "\n"
              << "rejected        : " << stats.rejected << "\n"
              << "cancels         : " << stats.cancels << "\n"
              << "order books     : " << (consistent ? "consistent (sorted, not crossed)" : "PROBLEM: " + why) << "\n"
              << "audit log       : " << options.logPath << "\n";
    return consistent ? 0 : 2;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parseArgs(argc, argv);
        std::signal(SIGINT, onSignal);
        enableAnsi();

        const int code = runSimulation(options);  // every object of the exchange is destroyed when this returns

        // The overloaded Order::operator new / delete count live orders: after shutdown this must be zero.
        std::cout << "live Order objects after shutdown: " << Order::liveOrders() << "\n" << std::flush;

        // The keyboard thread may still be blocked inside std::getline(std::cin). A normal return would make the
        // C++ runtime wait for the stdin lock that thread holds, so the program would hang. Everything of ours is
        // already destroyed at this point, so leaving immediately is safe.
        std::_Exit(code);
    } catch (const StockSimError& error) {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
}
