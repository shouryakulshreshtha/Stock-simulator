// ManualTrader.h - you, typing orders in the terminal during the demo.
//
//   BUY  RELIANCE 10 2450      limit order
//   SELL TCS 5 MKT             market order
//   CANCEL RELIANCE 1234       cancel one of your own orders (use the #id shown in the events)
//   QUIT                       stop the program
#pragma once
#include <atomic>
#include <mutex>
#include <string>

#include "Participant.h"

class ManualTrader : public Trader {
public:
    ManualTrader(std::string id, std::string name);

    // A manual trader does not decide by itself: the human does. So this returns nothing.
    std::vector<Command> decide(const MarketState& market, std::mt19937& rng) override;

    std::string status() const;
    bool quitRequested() const { return quit_.load(); }
    void setPromptRow(int row) { promptRow_ = row; }  // screen row of the "> " prompt (0 = no cursor control)

protected:
    void run(ThreadSafeQueue<Command>& queue, const MarketState& market) override;
    bool canBeJoined() const override { return false; }  // blocked in getline: cannot be woken, so it is detached

private:
    void handleLine(const std::string& line, ThreadSafeQueue<Command>& queue, const MarketState& market);
    void setStatus(const std::string& text);

    mutable std::mutex mutex_;
    std::string status_{"type a command and press Enter"};
    std::atomic<bool> quit_{false};
    int promptRow_{0};
};
