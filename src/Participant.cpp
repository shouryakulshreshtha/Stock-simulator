#include "Participant.h"

#include <chrono>

Trader::Trader(std::string id, std::string name, int intervalMillis, unsigned seed)
    : Participant(std::move(id), std::move(name)), intervalMillis_(intervalMillis), seed_(seed) {}

Trader::~Trader() { stop(); }

void Trader::start(ThreadSafeQueue<Command>& queue, const MarketState& market) {
    if (running_.exchange(true)) return;
    thread_ = std::thread([this, &queue, &market] { run(queue, market); });
}

void Trader::stop() {
    running_ = false;
    if (!thread_.joinable()) return;
    if (canBeJoined())
        thread_.join();
    else
        thread_.detach();  // e.g. a thread blocked on keyboard input cannot be woken up
}

void Trader::run(ThreadSafeQueue<Command>& queue, const MarketState& market) {
    std::mt19937 rng(seed_);
    while (running_) {
        for (Command& command : decide(market, rng)) queue.push(std::move(command));

        // Wait roughly intervalMillis_, with some randomness so the bots do not move in lockstep.
        std::uniform_int_distribution<int> jitter(intervalMillis_ / 2, intervalMillis_ * 3 / 2);
        const int wait = jitter(rng);
        for (int waited = 0; waited < wait && running_; waited += 20)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}
