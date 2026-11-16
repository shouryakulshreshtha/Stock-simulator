// tests/test_main.cpp - unit tests, no test framework needed.   Build and run with:  make test
// Each test_ function checks one behaviour. CHECK counts passes and failures.
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <thread>

#include "Bots.h"
#include "ChartExporter.h"
#include "Dashboard.h"
#include "Errors.h"
#include "EventLogger.h"
#include "MarketDataFeed.h"
#include "MatchingEngine.h"
#include "OrderBook.h"
#include "Portfolio.h"
#include "RiskManager.h"
#include "ThreadSafeQueue.h"
#include "Utils.h"

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(condition)                                                                    \
    do {                                                                                    \
        ++g_checks;                                                                         \
        if (!(condition)) {                                                                 \
            ++g_failures;                                                                   \
            std::cerr << "  FAIL  line " << __LINE__ << ":  " #condition "\n";             \
        }                                                                                   \
    } while (0)

#define CHECK_THROWS(expression, ExceptionType)                                             \
    do {                                                                                    \
        ++g_checks;                                                                         \
        bool caught = false;                                                                \
        try {                                                                               \
            expression;                                                                     \
        } catch (const ExceptionType&) {                                                    \
            caught = true;                                                                  \
        } catch (...) {                                                                     \
        }                                                                                   \
        if (!caught) {                                                                      \
            ++g_failures;                                                                   \
            std::cerr << "  FAIL  line " << __LINE__ << ":  expected " #ExceptionType "\n"; \
        }                                                                                   \
    } while (0)

namespace {

using LimitPtr = std::unique_ptr<LimitOrder>;
LimitPtr limit(const std::string& trader, Side side, int quantity, double price, const std::string& symbol = "RELIANCE") {
    return std::make_unique<LimitOrder>(symbol, side, quantity, toPaise(price), trader);
}

// Remembers everything the engine reports.
struct Recorder : TradeListener {
    std::vector<Trade> trades;
    std::vector<std::string> events;
    void onTrade(const Trade& trade) override { trades.push_back(trade); }
    void onOrderEvent(const OrderEvent& event) override { events.push_back(event.type); }
    int count(const std::string& type) const {
        int n = 0;
        for (const auto& e : events) n += (e == type);
        return n;
    }
};

// A small exchange without threads: tests call engine.process() directly.
struct Exchange {
    MarketState market;
    RiskManager risk{market};
    ThreadSafeQueue<Command> queue;
    MatchingEngine engine{queue, risk, market};
    Recorder recorder;

    Exchange() {
        market.addSymbol("RELIANCE", toPaise(2450));
        market.addSymbol("TCS", toPaise(3300));
        engine.addSymbol("RELIANCE");
        engine.addSymbol("TCS");
        engine.addListener(&recorder);
    }
    Portfolio& trader(const std::string& id, double cash, int shares) {
        Portfolio& p = risk.addTrader(id, toPaise(cash));
        p.addShares("RELIANCE", shares);
        p.addShares("TCS", shares);
        engine.addListener(&p);
        return p;
    }
};

std::string tempPath(const std::string& name) { return (std::filesystem::temp_directory_path() / name).string(); }

// ------------------------------------------------------------------ order book

void test_worked_example_from_the_report() {
    OrderBook book("RELIANCE");
    book.submit(limit("A", Side::Sell, 10, 2450), 1);
    book.submit(limit("B", Side::Sell, 5, 2450), 2);
    book.submit(limit("C", Side::Sell, 20, 2452), 3);
    MatchResult r = book.submit(limit("D", Side::Buy, 18, 2451), 4);

    CHECK(r.trades.size() == 2);
    CHECK(r.trades[0].quantity() == 10 && r.trades[0].price() == toPaise(2450) && r.trades[0].seller() == "A");
    CHECK(r.trades[1].quantity() == 5 && r.trades[1].price() == toPaise(2450) && r.trades[1].seller() == "B");
    CHECK(r.rested);
    CHECK(book.bestBid() == toPaise(2451));
    CHECK(book.snapshot(5).bids[0].quantity == 3);
    CHECK(book.snapshot(5).asks.size() == 1);
    CHECK(book.checkInvariants());
}

void test_price_priority() {
    OrderBook book("RELIANCE");
    book.submit(limit("A", Side::Sell, 5, 2451), 1);  // worse price, arrived first
    book.submit(limit("B", Side::Sell, 5, 2450), 2);  // better price, arrived later
    MatchResult r = book.submit(limit("C", Side::Buy, 5, 2455), 3);
    CHECK(r.trades.size() == 1 && r.trades[0].seller() == "B" && r.trades[0].price() == toPaise(2450));
}

void test_time_priority() {
    OrderBook book("RELIANCE");
    book.submit(limit("A", Side::Sell, 5, 2450), 1);
    book.submit(limit("B", Side::Sell, 5, 2450), 2);
    MatchResult r = book.submit(limit("C", Side::Buy, 5, 2450), 3);
    CHECK(r.trades.size() == 1 && r.trades[0].seller() == "A");
    CHECK(book.find(r.trades[0].sellOrderId()) == nullptr);  // A is gone
    CHECK(book.snapshot(5).asks[0].quantity == 5);            // B still waits
}

void test_partial_fill_keeps_the_rest_in_the_book() {
    OrderBook book("RELIANCE");
    book.submit(limit("A", Side::Sell, 10, 2450), 1);
    MatchResult r = book.submit(limit("B", Side::Buy, 4, 2450), 2);
    CHECK(r.trades.size() == 1 && r.trades[0].quantity() == 4);
    CHECK(!r.rested);
    CHECK(book.snapshot(5).asks[0].quantity == 6);
}

void test_limit_order_that_does_not_cross_rests() {
    OrderBook book("RELIANCE");
    book.submit(limit("A", Side::Sell, 10, 2455), 1);
    MatchResult r = book.submit(limit("B", Side::Buy, 10, 2450), 2);
    CHECK(r.trades.empty() && r.rested);
    CHECK(book.bestBid() == toPaise(2450) && book.bestAsk() == toPaise(2455));
}

void test_market_order_walks_the_book_and_respects_its_limit() {
    OrderBook book("RELIANCE");
    book.submit(limit("A", Side::Sell, 5, 2450), 1);
    book.submit(limit("B", Side::Sell, 5, 2460), 2);
    MatchResult r = book.submit(std::make_unique<MarketOrder>("RELIANCE", Side::Buy, 20, toPaise(2455), "C"), 3);
    CHECK(r.trades.size() == 1 && r.trades[0].quantity() == 5);  // 2460 is beyond its protection price
    CHECK(r.dropped && r.dropped->quantity() == 15);
    CHECK(!r.rested);
    CHECK(book.snapshot(5).asks.size() == 1);
}

void test_cancel() {
    OrderBook book("RELIANCE");
    LimitPtr order = limit("A", Side::Buy, 10, 2440);
    const OrderId id = order->id();
    book.submit(std::move(order), 1);
    CHECK(book.find(id) != nullptr);
    std::unique_ptr<Order> removed = book.cancel(id);
    CHECK(removed && removed->id() == id);
    CHECK(book.cancel(id) == nullptr);  // second cancel finds nothing
    CHECK(book.openOrders() == 0);
}

void test_self_trade_prevention() {
    OrderBook book("RELIANCE");
    book.submit(limit("A", Side::Sell, 10, 2450), 1);
    MatchResult r = book.submit(limit("A", Side::Buy, 10, 2451), 2);
    CHECK(r.trades.empty());
    CHECK(r.cancelled.size() == 1);
    CHECK(r.rested);
    CHECK(book.checkInvariants());
}

void test_invalid_orders_are_refused() {
    CHECK_THROWS(LimitOrder("RELIANCE", Side::Buy, 0, toPaise(2450), "A"), InvalidOrderError);
    CHECK_THROWS(LimitOrder("RELIANCE", Side::Buy, -5, toPaise(2450), "A"), InvalidOrderError);
    CHECK_THROWS(LimitOrder("RELIANCE", Side::Buy, 5, 0, "A"), InvalidOrderError);
    CHECK_THROWS(LimitOrder("RELIANCE", Side::Buy, 5, 245003, "A"), InvalidOrderError);  // not a multiple of 5 paise
    CHECK_THROWS(LimitOrder("", Side::Buy, 5, toPaise(2450), "A"), InvalidOrderError);
    CHECK_THROWS(MarketOrder("RELIANCE", Side::Buy, 5, 0, "A"), InvalidOrderError);
    OrderBook book("RELIANCE");
    CHECK_THROWS(book.submit(limit("A", Side::Buy, 5, 3300, "TCS"), 1), InvalidOrderError);  // wrong book
}

// ------------------------------------------------------------------ engine + risk + portfolio

void test_trade_settles_cash_and_shares() {
    Exchange x;
    Portfolio& seller = x.trader("A", 100000, 100);
    Portfolio& buyer = x.trader("B", 100000, 100);
    x.engine.submit(limit("A", Side::Sell, 10, 2450));
    CHECK(seller.reservedShares("RELIANCE") == 10);  // shares are locked while the order waits
    x.engine.submit(limit("B", Side::Buy, 10, 2451));  // pays 2450: the price of the waiting order

    CHECK(x.recorder.trades.size() == 1);
    CHECK(seller.cash() == toPaise(100000 + 24500) && seller.shares("RELIANCE") == 90);
    CHECK(buyer.cash() == toPaise(100000 - 24500) && buyer.shares("RELIANCE") == 110);
    CHECK(seller.reservedShares("RELIANCE") == 0);
    CHECK(buyer.reservedCash() == 0);  // the extra 10 paise per share that was locked came back
}

void test_risk_refuses_orders_nobody_can_pay_for() {
    Exchange x;
    x.trader("POOR", 1000, 0);
    x.engine.submit(limit("POOR", Side::Buy, 10, 2450));   // needs 24,500
    x.engine.submit(limit("POOR", Side::Sell, 5, 2450));   // owns no shares
    CHECK(x.engine.stats().rejected == 2);
    CHECK(x.engine.stats().orders == 0);
    CHECK(x.recorder.count("REJECT") == 2);
}

void test_cash_cannot_be_spent_twice() {
    Exchange x;
    Portfolio& p = x.trader("A", 30000, 0);
    x.engine.submit(limit("A", Side::Buy, 10, 2450));  // locks 24,500
    CHECK(p.reservedCash() == toPaise(24500));
    x.engine.submit(limit("A", Side::Buy, 10, 2450));  // only 5,500 free: refused
    CHECK(x.engine.stats().rejected == 1);
}

void test_cancel_releases_the_reservation() {
    Exchange x;
    Portfolio& p = x.trader("A", 100000, 0);
    LimitPtr order = limit("A", Side::Buy, 10, 2450);
    const OrderId id = order->id();
    x.engine.submit(std::move(order));
    CHECK(p.reservedCash() == toPaise(24500));
    x.engine.cancel("A", "RELIANCE", id);
    CHECK(p.reservedCash() == 0);
    CHECK(x.engine.stats().cancels == 1);
    x.engine.cancel("A", "RELIANCE", id);  // already gone: reported, not an error
    CHECK(x.recorder.count("CANCEL_MISS") == 1);
    CHECK(x.engine.stats().rejected == 0);
}

void test_only_the_owner_can_cancel() {
    Exchange x;
    x.trader("A", 100000, 0);
    x.trader("B", 100000, 0);
    LimitPtr order = limit("A", Side::Buy, 10, 2450);
    const OrderId id = order->id();
    x.engine.submit(std::move(order));
    x.engine.cancel("B", "RELIANCE", id);
    CHECK(x.engine.stats().cancels == 0);
    CHECK(x.engine.stats().rejected == 1);
}

void test_price_band() {
    Exchange x;
    x.trader("A", 10000000, 0);
    x.engine.submit(limit("A", Side::Buy, 1, 3000));  // reference is 2450, band is +-20% (1960 to 2940)
    CHECK(x.engine.stats().rejected == 1);
    x.engine.submit(limit("A", Side::Buy, 1, 2900));
    CHECK(x.engine.stats().orders == 1);
}

void test_unknown_symbol_is_rejected_not_fatal() {
    Exchange x;
    x.trader("A", 100000, 0);
    x.engine.submit(limit("A", Side::Buy, 1, 100, "NOPE"));
    CHECK(x.engine.stats().rejected == 1);
}

void test_market_order_unfilled_part_is_released() {
    Exchange x;
    Portfolio& buyer = x.trader("B", 1000000, 0);
    x.trader("A", 0, 100);
    x.engine.submit(limit("A", Side::Sell, 5, 2450));
    x.engine.submit(std::make_unique<MarketOrder>("RELIANCE", Side::Buy, 20, toPaise(2500), "B"));
    CHECK(x.recorder.trades.size() == 1 && x.recorder.trades[0].quantity() == 5);
    CHECK(x.recorder.count("UNFILLED") == 1);
    CHECK(buyer.reservedCash() == 0);
    CHECK(buyer.shares("RELIANCE") == 5);
}

// A long random run: whatever happens, books stay valid and no money or shares appear or vanish.
void test_random_stress_keeps_every_rule() {
    Exchange x;
    const std::vector<std::string> names = {"T0", "T1", "T2", "T3", "T4", "T5"};
    for (const auto& n : names) x.trader(n, 5'000'000, 500);
    const Paise cashBefore = [&] { Paise s = 0; for (auto* p : x.risk.portfolios()) s += p->cash(); return s; }();

    std::mt19937 rng(12345);
    std::vector<std::tuple<std::string, std::string, OrderId>> issued;
    for (int i = 0; i < 20000; ++i) {
        const std::string trader = names[rng() % names.size()];
        const std::string symbol = (rng() % 2) ? "RELIANCE" : "TCS";
        const double ref = (symbol == "RELIANCE") ? 2450 : 3300;
        const int roll = static_cast<int>(rng() % 10);
        if (roll < 6) {
            auto order = limit(trader, (rng() % 2) ? Side::Buy : Side::Sell, 1 + static_cast<int>(rng() % 40),
                               ref * (1.0 + (static_cast<int>(rng() % 200) - 100) / 10000.0), symbol);
            issued.emplace_back(trader, symbol, order->id());
            x.engine.submit(std::move(order));
        } else if (roll < 8) {
            const Side side = (rng() % 2) ? Side::Buy : Side::Sell;
            x.engine.submit(std::make_unique<MarketOrder>(symbol, side, 1 + static_cast<int>(rng() % 60),
                                                         toPaise(ref * (side == Side::Buy ? 1.02 : 0.98)), trader));
        } else if (!issued.empty()) {
            const auto& [t, s, id] = issued[rng() % issued.size()];
            x.engine.cancel(t, s, id);
        }
    }

    std::string why;
    CHECK(x.engine.checkInvariants(&why));
    if (!why.empty()) std::cerr << "  " << why << "\n";
    CHECK(x.engine.stats().trades > 500);  // the run really traded

    Paise cashAfter = 0;
    int relianceShares = 0, tcsShares = 0;
    for (auto* p : x.risk.portfolios()) {
        cashAfter += p->cash();
        relianceShares += p->shares("RELIANCE");
        tcsShares += p->shares("TCS");
        CHECK(p->cash() - p->reservedCash() >= 0);                 // never promised more cash than owned
        CHECK(p->shares("RELIANCE") - p->reservedShares("RELIANCE") >= 0);
        CHECK(p->shares("TCS") - p->reservedShares("TCS") >= 0);
        CHECK(p->shares("RELIANCE") >= 0 && p->shares("TCS") >= 0);  // no short selling
    }
    CHECK(cashAfter == cashBefore);        // trades only move money between traders
    CHECK(relianceShares == 500 * 6);      // ... and shares
    CHECK(tcsShares == 500 * 6);
}

// ------------------------------------------------------------------ data

void test_csv_loader_reads_columns_by_name_and_skips_bad_rows() {
    const std::string path = tempPath("stocksim_test_prices.csv");
    {
        std::ofstream f(path);
        f << "Symbol,Close,Date,Volume,Open\n"  // columns in a different order than the Kaggle files
             "X,100.50,2020-01-01,1000,99\n"
             "X,,2020-01-02,1000,100\n"          // empty close: skipped
             "X,abc,2020-01-03,1000,100\n"       // text instead of a number: skipped
             "X,-4,2020-01-04,1000,100\n"        // negative price: skipped
             "X,102.25,2020-01-05,1500,101\n";
    }
    CsvPriceSource source(path);
    CHECK(source.rows() == 2);
    CHECK(source.skipped() == 3);
    PriceTick t;
    CHECK(source.next(t) && t.date == "2020-01-01" && t.close == 100.50 && t.volume == 1000);
    CHECK(source.next(t) && t.close == 102.25);
    CHECK(!source.next(t));
    source.rewind();
    CHECK(source.next(t) && t.date == "2020-01-01");
    std::remove(path.c_str());
}

void test_csv_loader_errors() {
    CHECK_THROWS(CsvPriceSource("/no/such/file.csv"), DataFileError);

    const std::string path = tempPath("stocksim_test_nocolumn.csv");
    { std::ofstream f(path); f << "Date,Price\n2020-01-01,5\n"; }
    CHECK_THROWS(CsvPriceSource{path}, DataFileError);  // no Close column
    { std::ofstream f(path); f << "Date,Close\n2020-01-01,\n"; }
    CHECK_THROWS(CsvPriceSource{path}, DataFileError);  // no usable rows
    std::remove(path.c_str());
}

void test_the_sample_file_in_kaggle_format_loads() {
    const char* candidates[] = {"data/sample/SAMPLE_kaggle_format.csv", "../data/sample/SAMPLE_kaggle_format.csv"};
    for (const char* path : candidates) {
        if (!std::filesystem::exists(path)) continue;
        CsvPriceSource source(path);
        CHECK(source.rows() == 118 && source.skipped() == 2);
        return;
    }
    std::cout << "  (sample CSV not found from this folder, skipped)\n";
}

void test_price_tick_stream_operator() {
    PriceTick t;
    std::istringstream good("2020-01-01,10,12,9,11.5,300");
    CHECK(static_cast<bool>(good >> t) && t.close == 11.5 && t.volume == 300);
    std::istringstream bad("2020-01-01,10,12,nine,11.5,300");
    CHECK(!(bad >> t));
}

void test_generated_source_and_feed() {
    MarketState market;
    MarketDataFeed feed(market, 20, 4);
    feed.addSource("TCS", std::make_unique<GeneratedPriceSource>(3300.0, 1));
    feed.prime();
    CHECK(market.hasSymbol("TCS"));
    CHECK(market.reference("TCS") >= toPaise(3300.0 * 0.93) && market.reference("TCS") <= toPaise(3300.0 * 1.07));  // first day moves at most 7%
    feed.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    feed.stop();
    CHECK(market.reference("TCS") > 0);
    CHECK(market.dateLabel().rfind("SIM-", 0) == 0);
}

// ------------------------------------------------------------------ classes and other pieces

void test_virtual_base_gives_one_participant() {
    RandomBot bot("Retail-1", 100, 1, {"RELIANCE"});
    Participant* viaTrader = static_cast<Trader*>(&bot);
    Participant* viaAuditable = static_cast<Auditable*>(&bot);
    CHECK(viaTrader == viaAuditable);  // one shared Participant: the diamond problem is solved
    CHECK(bot.id() == "Retail-1");
    bot.noteSent(3);
    CHECK(bot.sent() == 3);
    CHECK(bot.auditLine().find("3") != std::string::npos);
}

void test_bots_only_make_valid_commands() {
    MarketState market;
    market.addSymbol("RELIANCE", toPaise(2450));
    market.addSymbol("TCS", toPaise(3300));
    std::vector<std::string> symbols = {"RELIANCE", "TCS"};
    RandomBot random("R", 100, 1, symbols);
    MomentumBot momentum("M", 100, 2, symbols);
    MarketMakerBot maker("MM", 100, 3, symbols);
    std::mt19937 rng(9);
    std::vector<Trader*> all = {&random, &momentum, &maker};
    int produced = 0;
    for (int round = 0; round < 300; ++round) {
        for (Trader* t : all) {
            if (round % 40 == 0) market.setReference("RELIANCE", toPaise(2450.0 + round), "");  // a trend for the momentum bot
            for (Command& c : t->decide(market, rng)) {
                ++produced;
                if (c.type == Command::Type::Submit) {
                    CHECK(c.order && c.order->quantity() > 0 && c.order->limitPrice() > 0);
                    CHECK(c.order->limitPrice() % TICK_PAISE == 0);
                } else {
                    CHECK(c.cancelId != 0);
                }
            }
        }
    }
    CHECK(produced > 300);
}

void test_portfolio_copy_and_plus_equals() {
    Portfolio original("A", toPaise(1000));
    original.addShares("RELIANCE", 10);
    Portfolio copy(original);  // copy constructor
    Trade t("RELIANCE", 1, 1, 2, "B", "A", 4, toPaise(100));  // A sells 4 shares at 100
    original += t;
    CHECK(original.cash() == toPaise(1400) && original.shares("RELIANCE") == 6);
    CHECK(copy.cash() == toPaise(1000) && copy.shares("RELIANCE") == 10);  // the copy did not change
    Portfolio moved(std::move(copy));
    CHECK(moved.shares("RELIANCE") == 10);
}

void test_order_counting_new_and_delete() {
    const long before = Order::liveOrders();
    {
        auto a = limit("A", Side::Buy, 1, 100);
        std::unique_ptr<Order> b = std::make_unique<MarketOrder>("RELIANCE", Side::Sell, 1, toPaise(100), "B");
        CHECK(Order::liveOrders() == before + 2);
        std::ostringstream os;
        os << *a;  // overloaded <<
        CHECK(os.str().find("LIMIT BUY 1 RELIANCE") != std::string::npos);
    }
    CHECK(Order::liveOrders() == before);  // deleting through the base pointer freed both
}

void test_order_less_than_means_earlier() {
    LimitOrder a("RELIANCE", Side::Buy, 1, toPaise(100), "A");
    LimitOrder b("RELIANCE", Side::Buy, 1, toPaise(100), "B");
    a.setArrival(1);
    b.setArrival(2);
    CHECK(a < b && !(b < a));
}

void test_templates() {
    CHECK(clampValue(15, 0, 10) == 10 && clampValue(-3, 0, 10) == 0 && clampValue(5, 0, 10) == 5);
    CHECK(clampValue(2.5, 1.0, 2.0) == 2.0);
    RollingAverage<int> avg(3);
    avg.add(1); avg.add(2);
    CHECK(!avg.ready());
    avg.add(3); avg.add(4);  // window now holds 2, 3, 4
    CHECK(avg.ready() && avg.average() == 3.0);
}

void test_queue_with_many_threads() {
    ThreadSafeQueue<int> queue;
    std::atomic<long> sum{0};
    std::thread consumer([&] { int v; while (queue.pop(v)) sum += v; });
    std::vector<std::thread> producers;
    for (int p = 0; p < 4; ++p)
        producers.emplace_back([&] { for (int i = 1; i <= 1000; ++i) queue.push(i); });
    for (auto& t : producers) t.join();
    queue.close();
    consumer.join();
    CHECK(sum == 4L * (1000L * 1001L / 2));
    CHECK(!queue.push(1));  // closed
}

void test_the_engine_thread_processes_the_queue() {
    Exchange x;
    x.trader("A", 100000, 100);
    x.trader("B", 100000, 100);
    x.engine.start();
    x.queue.push(Command::submit(limit("A", Side::Sell, 10, 2450)));
    x.queue.push(Command::submit(limit("B", Side::Buy, 10, 2450)));
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    x.engine.stop();
    CHECK(x.recorder.trades.size() == 1);
}

void test_event_logger_and_chart_files() {
    const std::string log = tempPath("stocksim_test_events.csv");
    const std::string chartFile = tempPath("stocksim_test_chart.js");
    {
        Exchange x;
        EventLogger logger(log);
        ChartExporter chart(x.engine, x.market, chartFile);
        x.engine.addListener(&logger);
        x.engine.addListener(&chart);
        x.trader("A", 100000, 100);
        x.trader("B", 100000, 100);
        x.engine.submit(limit("A", Side::Sell, 10, 2450));
        x.engine.submit(limit("B", Side::Buy, 10, 2450));
        chart.flush();
    }
    std::ifstream in(log);
    std::string all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(all.rfind("time,event,symbol", 0) == 0);
    CHECK(all.find(",TRADE,RELIANCE,") != std::string::npos);
    CHECK(all.find(",SUBMIT,RELIANCE,") != std::string::npos);

    std::ifstream js(chartFile);
    std::string chartText((std::istreambuf_iterator<char>(js)), std::istreambuf_iterator<char>());
    CHECK(chartText.find("window.STOCKSIM") != std::string::npos);
    CHECK(chartText.find("\"RELIANCE\"") != std::string::npos);

    CHECK_THROWS(EventLogger("/no/such/folder/log.csv"), DataFileError);
    std::remove(log.c_str());
    std::remove(chartFile.c_str());
}

void test_dashboard_frame_has_a_fixed_size() {
    Exchange x;
    x.trader("A", 100000, 100);
    x.trader("B", 100000, 100);
    Dashboard dash(x.engine, x.market, x.risk, "test data");
    x.engine.addListener(&dash);
    x.engine.submit(limit("A", Side::Sell, 10, 2450));
    x.engine.submit(limit("B", Side::Buy, 10, 2450));
    x.engine.submit(limit("B", Side::Buy, 10, 2400));
    const std::string frame = dash.render(false);
    int lines = 0;
    for (char c : frame) lines += (c == '\n');
    CHECK(lines == Dashboard::kFrameLines);
    CHECK(frame.find("ORDER BOOK") != std::string::npos);
    CHECK(frame.find("RECENT TRADES") != std::string::npos);
    CHECK(frame.find("RELIANCE") != std::string::npos);
}

}  // namespace

int main() {
    struct Test { const char* name; void (*run)(); };
    const Test tests[] = {
        {"worked example from the report", test_worked_example_from_the_report},
        {"price priority", test_price_priority},
        {"time priority", test_time_priority},
        {"partial fill keeps the rest", test_partial_fill_keeps_the_rest_in_the_book},
        {"limit order that does not cross rests", test_limit_order_that_does_not_cross_rests},
        {"market order walks the book, respects its limit", test_market_order_walks_the_book_and_respects_its_limit},
        {"cancel", test_cancel},
        {"self-trade prevention", test_self_trade_prevention},
        {"invalid orders are refused", test_invalid_orders_are_refused},
        {"trade settles cash and shares", test_trade_settles_cash_and_shares},
        {"risk refuses unaffordable orders", test_risk_refuses_orders_nobody_can_pay_for},
        {"cash cannot be spent twice", test_cash_cannot_be_spent_twice},
        {"cancel releases the reservation", test_cancel_releases_the_reservation},
        {"only the owner can cancel", test_only_the_owner_can_cancel},
        {"price band", test_price_band},
        {"unknown symbol rejected, not fatal", test_unknown_symbol_is_rejected_not_fatal},
        {"unfilled market order releases its lock", test_market_order_unfilled_part_is_released},
        {"random stress: invariants, cash and shares conserved", test_random_stress_keeps_every_rule},
        {"csv loader: by name, bad rows skipped", test_csv_loader_reads_columns_by_name_and_skips_bad_rows},
        {"csv loader: errors", test_csv_loader_errors},
        {"sample file in Kaggle format loads", test_the_sample_file_in_kaggle_format_loads},
        {"operator>> for a price row", test_price_tick_stream_operator},
        {"generated prices and the feed thread", test_generated_source_and_feed},
        {"virtual base: one Participant", test_virtual_base_gives_one_participant},
        {"bots only make valid commands", test_bots_only_make_valid_commands},
        {"portfolio copy, move and +=", test_portfolio_copy_and_plus_equals},
        {"new/delete counting and <<", test_order_counting_new_and_delete},
        {"Order::operator< means earlier", test_order_less_than_means_earlier},
        {"templates", test_templates},
        {"queue with many threads", test_queue_with_many_threads},
        {"engine thread processes the queue", test_the_engine_thread_processes_the_queue},
        {"event log and chart file", test_event_logger_and_chart_files},
        {"dashboard frame has a fixed size", test_dashboard_frame_has_a_fixed_size},
    };

    int failedTests = 0;
    for (const Test& test : tests) {
        const int before = g_failures;
        try {
            test.run();
        } catch (const std::exception& error) {
            ++g_failures;
            std::cerr << "  EXCEPTION: " << error.what() << "\n";
        }
        const bool ok = (g_failures == before);
        if (!ok) ++failedTests;
        std::cout << (ok ? "[ ok ]  " : "[FAIL]  ") << test.name << "\n";
    }

    std::cout << "\n" << (sizeof(tests) / sizeof(tests[0])) << " tests, " << g_checks << " checks, " << g_failures
              << " failures; live Order objects at the end: " << Order::liveOrders() << "\n";
    return g_failures == 0 && Order::liveOrders() == 0 ? 0 : 1;
}
