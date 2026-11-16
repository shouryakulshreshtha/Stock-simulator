// Errors.h - exception classes (syllabus: exception handling).
// Every error of ours derives from StockSimError, which derives from std::runtime_error,
// so one catch (const StockSimError&) handles all of them.
#pragma once
#include <stdexcept>
#include <string>

class StockSimError : public std::runtime_error {
public:
    explicit StockSimError(const std::string& message) : std::runtime_error(message) {}
};

// The order itself is wrong: negative quantity, bad price, unknown symbol...
class InvalidOrderError : public StockSimError {
public:
    explicit InvalidOrderError(const std::string& message) : StockSimError(message) {}
};

// The order is well formed but the trader may not place it: no cash, no shares, outside price band...
class RiskError : public StockSimError {
public:
    explicit RiskError(const std::string& message) : StockSimError(message) {}
};

// A data file is missing or unusable.
class DataFileError : public StockSimError {
public:
    explicit DataFileError(const std::string& message) : StockSimError(message) {}
};
