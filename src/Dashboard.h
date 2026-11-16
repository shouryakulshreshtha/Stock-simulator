// Dashboard.h - the live terminal screen: market table, order book, recent trades, portfolios, events.
// It is a TradeListener (Observer pattern): the engine pushes trades and events to it, and the main loop
// asks it to draw a new frame every half second.
#pragma once
#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "MatchingEngine.h"
#include "Participant.h"
#include "Portfolio.h"
#include "RiskManager.h"
#include "TradeListener.h"

class Dashboard : public TradeListener {
public:
    static constexpr int kFrameLines = 30;  // the frame always has exactly this many lines

    Dashboard(const MatchingEngine& engine, const MarketState& market, const RiskManager& risk, std::string dataLine);

    void addAuditable(const Auditable* trader);                  // to show how many commands each bot sent
    void setStatusSource(std::function<std::string()> source);  // line shown for the manual trader

    void onTrade(const Trade& trade) override;
    void onOrderEvent(const OrderEvent& event) override;

    // ansi = true: draw in place using terminal escape codes. false: plain text (for logs and tests).
    std::string render(bool ansi);

    // Function overloading: show() turns different things into one formatted line.
    static std::string show(const Trade& trade);
    static std::string show(const Portfolio::Summary& summary, long commandsSent);

private:
    const MatchingEngine& engine_;
    const MarketState& market_;
    const RiskManager& risk_;
    std::string dataLine_;
    std::map<std::string, const Auditable*> auditables_;
    std::function<std::string()> statusSource_;

    std::mutex mutex_;  // protects the two deques below (engine thread writes, main thread reads)
    std::deque<std::string> trades_;
    std::deque<std::string> events_;

    std::chrono::steady_clock::time_point start_;
    std::chrono::steady_clock::time_point lastRender_;
    long lastOrders_{0};
    double ordersPerSecond_{0.0};
};
