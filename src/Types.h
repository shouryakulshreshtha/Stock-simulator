// Types.h - basic types shared by every module.
#pragma once
#include <cmath>
#include <cstdint>

using OrderId = std::uint64_t;

// Money is kept as an integer number of paise (1 rupee = 100 paise).
// Floating point would give 0.1 + 0.2 != 0.3 style errors inside the order book.
using Paise = long long;

constexpr Paise TICK_PAISE = 5;  // NSE tick size is 5 paise

enum class Side { Buy, Sell };

inline const char* toString(Side s) { return s == Side::Buy ? "BUY" : "SELL"; }

// Rupees -> paise, rounded to the nearest valid tick.
inline Paise toPaise(double rupees) { return std::llround(rupees * 20.0) * TICK_PAISE; }

inline double toRupees(Paise p) { return static_cast<double>(p) / 100.0; }
