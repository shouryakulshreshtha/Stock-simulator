// Utils.h - small helpers, including the two templates of the project.
#pragma once
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <ctime>
#include <deque>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>

#include "Types.h"

// Function template: keep a value between low and high.
template <typename T>
T clampValue(T value, T low, T high) {
    return value < low ? low : (value > high ? high : value);
}

// Class template: average of the last N values (used by MomentumBot to see a trend).
template <typename T>
class RollingAverage {
public:
    explicit RollingAverage(std::size_t window = 20) : window_(window) {}

    void add(T value) {
        values_.push_back(value);
        sum_ += value;
        if (values_.size() > window_) {
            sum_ -= values_.front();
            values_.pop_front();
        }
    }
    bool ready() const { return values_.size() >= window_; }
    double average() const {
        return values_.empty() ? 0.0 : static_cast<double>(sum_) / static_cast<double>(values_.size());
    }

private:
    std::size_t window_;
    std::deque<T> values_;
    T sum_{};
};

inline std::string timeStamp() {
    std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    std::ostringstream os;
    os << std::put_time(&local, "%H:%M:%S");
    return os.str();
}

inline std::string toUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

inline std::string formatPrice(Paise p) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(2) << toRupees(p);
    return os.str();
}

// Both the dashboard and the manual trader write to the terminal; this keeps their output from mixing.
inline std::mutex& consoleMutex() {
    static std::mutex m;
    return m;
}
