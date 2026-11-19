// Exceptions.hpp - custom exception hierarchy (Unit IV: exception handling).
//
// Every error the casino can raise derives from CasinoException, which itself
// derives from std::exception. Callers can therefore catch narrowly
// (InsufficientFundsException) or broadly (CasinoException / std::exception).
#pragma once

#include <exception>
#include <string>
#include <utility>

namespace casino {

class CasinoException : public std::exception {
public:
    explicit CasinoException(std::string message) : message_(std::move(message)) {}
    const char* what() const noexcept override { return message_.c_str(); }

private:
    std::string message_;
};

// Thrown when an account cannot cover a transfer.
class InsufficientFundsException : public CasinoException {
public:
    InsufficientFundsException(const std::string& account, long needed, long available)
        : CasinoException("Insufficient funds in '" + account + "': need " +
                          std::to_string(needed) + " chips, have " + std::to_string(available)),
          needed_(needed),
          available_(available) {}
    long needed() const { return needed_; }
    long available() const { return available_; }

private:
    long needed_;
    long available_;
};

// Thrown for a bet or transfer amount that breaks the table rules.
class InvalidBetException : public CasinoException {
public:
    explicit InvalidBetException(const std::string& why) : CasinoException("Invalid bet: " + why) {}
};

// Thrown when typed input cannot be understood.
class InvalidInputException : public CasinoException {
public:
    explicit InvalidInputException(const std::string& why) : CasinoException(why) {}
};

// Thrown when stdin is closed (end of file) while the program waits for input.
class InputClosedException : public CasinoException {
public:
    InputClosedException() : CasinoException("Input stream closed.") {}
};

class EmptyDeckException : public CasinoException {
public:
    EmptyDeckException() : CasinoException("The deck is empty.") {}
};

class UnknownAccountException : public CasinoException {
public:
    explicit UnknownAccountException(const std::string& id)
        : CasinoException("Unknown ledger account '" + id + "'") {}
};

// Thrown when a saved file fails its integrity check.
class DataCorruptionException : public CasinoException {
public:
    explicit DataCorruptionException(const std::string& file, const std::string& why)
        : CasinoException("Data file '" + file + "' is corrupted: " + why) {}
};

class FileAccessException : public CasinoException {
public:
    explicit FileAccessException(const std::string& file, const std::string& why)
        : CasinoException("Cannot access file '" + file + "': " + why) {}
};

}  // namespace casino
