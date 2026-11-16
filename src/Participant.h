// Participant.h - the trader class family.
//
//                 Participant            <- VIRTUAL base class: id and name, shared by everybody
//                  /        \           (virtual inheritance)
//             Trader        Auditable
//          (runs a thread)  (counts orders sent)
//                  \        /
//                  BotTrader            <- MULTIPLE inheritance: a Trader AND Auditable
//                /     |      \   (the three bots)
//        RandomBot MomentumBot MarketMakerBot        (ManualTrader derives from Trader only)
//
// Without "virtual", BotTrader would contain TWO Participant objects (the diamond problem). With it there
// is one, and the MOST DERIVED class is the one that initialises it.
#pragma once
#include <atomic>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "Command.h"
#include "MarketState.h"
#include "ThreadSafeQueue.h"

class Participant {
public:
    Participant(std::string id, std::string name) : id_(std::move(id)), name_(std::move(name)) {}
    virtual ~Participant() = default;
    const std::string& id() const { return id_; }
    const std::string& name() const { return name_; }

private:
    std::string id_;
    std::string name_;
};

// A trader: has its own thread and sends commands to the engine through the queue.
class Trader : public virtual Participant {
public:
    Trader(std::string id, std::string name, int intervalMillis, unsigned seed);
    ~Trader() override;  // virtual (inherited); stops the thread
    Trader(const Trader&) = delete;
    Trader& operator=(const Trader&) = delete;

    void start(ThreadSafeQueue<Command>& queue, const MarketState& market);
    void stop();

    // Pure virtual: every kind of trader decides in its own way. Called again and again by the thread.
    virtual std::vector<Command> decide(const MarketState& market, std::mt19937& rng) = 0;

protected:
    virtual void run(ThreadSafeQueue<Command>& queue, const MarketState& market);  // default: loop around decide()
    virtual bool canBeJoined() const { return true; }

    int intervalMillis_;
    unsigned seed_;
    std::atomic<bool> running_{false};

private:
    std::thread thread_;
};

// Something that can report how much it did.
class Auditable : public virtual Participant {
public:
    Auditable(std::string id, std::string name) : Participant(std::move(id), std::move(name)) {}
    void noteSent(long count = 1) { sent_ += count; }
    long sent() const { return sent_.load(); }
    std::string auditLine() const { return name() + " sent " + std::to_string(sent()) + " commands"; }

private:
    std::atomic<long> sent_{0};
};
