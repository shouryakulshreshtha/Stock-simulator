// ThreadSafeQueue.h - class template. Many trader threads push, the one engine thread pops.
// Works for any type T (here: Command), the compiler builds a version for each T we use.
#pragma once
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <utility>

template <typename T>
class ThreadSafeQueue {
public:
    // Returns false when the queue is already closed (the item is dropped).
    bool push(T item) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_) return false;
            items_.push_back(std::move(item));
        }
        ready_.notify_one();
        return true;
    }

    // Waits for an item. Returns false when the queue is closed and empty (time to stop).
    bool pop(T& out) {
        std::unique_lock<std::mutex> lock(mutex_);
        ready_.wait(lock, [this] { return !items_.empty() || closed_; });
        if (items_.empty()) return false;
        out = std::move(items_.front());
        items_.pop_front();
        return true;
    }

    void close() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }
        ready_.notify_all();
    }

    std::size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return items_.size();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<T> items_;
    bool closed_{false};
};
