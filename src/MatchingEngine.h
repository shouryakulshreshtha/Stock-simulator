// MatchingEngine.h - owns one OrderBook per stock and is the ONLY code that changes the books.
// One engine thread takes commands from the queue one at a time, so two threads can never
// corrupt a book. Other threads (the dashboard) read through snapshot(), which takes the same lock.
#pragma once
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "Command.h"
#include "MarketState.h"
#include "OrderBook.h"
#include "RiskManager.h"
#include "ThreadSafeQueue.h"
#include "TradeListener.h"

class MatchingEngine {
public:
    struct Stats {
        long orders{0};
        long trades{0};
        long rejected{0};
        long cancels{0};
    };

    MatchingEngine(ThreadSafeQueue<Command>& queue, RiskManager& risk, MarketState& market);
    ~MatchingEngine();
    MatchingEngine(const MatchingEngine&) = delete;
    MatchingEngine& operator=(const MatchingEngine&) = delete;

    void addSymbol(const std::string& symbol);
    void addListener(TradeListener* listener);  // setup only, before start()

    void start();  // run the engine thread
    void stop();   // close the queue, wait for the thread

    // Handle one command now (the engine thread calls this for every queued command; tests call it directly).
    void process(Command&& command);

    // Function overloading: same name, different parameter types.
    void submit(std::unique_ptr<LimitOrder> order);
    void submit(std::unique_ptr<MarketOrder> order);
    void cancel(const std::string& trader, const std::string& symbol, OrderId id);

    BookSnapshot snapshot(const std::string& symbol, std::size_t depth) const;
    Stats stats() const;
    bool checkInvariants(std::string* why = nullptr) const;

private:
    void handleSubmit(std::unique_ptr<Order> order);
    void handleCancel(const Command& command);
    void emit(const OrderEvent& event);
    static OrderEvent makeEvent(const std::string& type, const Order& order, const std::string& detail = "");

    ThreadSafeQueue<Command>& queue_;
    RiskManager& risk_;
    MarketState& market_;
    std::vector<TradeListener*> listeners_;
    std::map<std::string, OrderBook> books_;
    mutable std::mutex bookMutex_;  // protects books_ (engine thread writes, dashboard reads)
    std::uint64_t arrival_{0};
    std::thread thread_;

    std::atomic<long> orders_{0};
    std::atomic<long> trades_{0};
    std::atomic<long> rejected_{0};
    std::atomic<long> cancels_{0};
};
