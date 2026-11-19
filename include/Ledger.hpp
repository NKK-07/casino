// Ledger.hpp - the casino's chip bank (play money only).
//
// Every chip lives in exactly one account. Chips only ever move through
// transfer(), so the sum of all balances never changes: audit() proves no chips
// were created or destroyed by any game.
#pragma once

#include <iosfwd>
#include <map>
#include <string>
#include <vector>

namespace casino {

struct Transaction {
    long id = 0;
    std::string from;
    std::string to;
    long amount = 0;
    std::string memo;
};

std::ostream& operator<<(std::ostream& os, const Transaction& tx);

class Ledger {
public:
    static const std::string HOUSE;  // the casino's bankroll
    static const std::string RAKE;   // poker rake collected

    explicit Ledger(long houseBankroll = 100000000L);

    // Idempotent: opening an existing account does nothing.
    void openAccount(const std::string& id);
    bool hasAccount(const std::string& id) const;
    // Throws UnknownAccountException.
    long balance(const std::string& id) const;

    // Moves chips between accounts. A zero amount is a no-op. Throws
    // InvalidBetException (negative), UnknownAccountException or
    // InsufficientFundsException; the ledger is unchanged when it throws.
    void transfer(const std::string& from, const std::string& to, long amount, const std::string& memo);

    long totalChips() const;
    long totalSupply() const { return supply_; }
    bool audit() const { return totalChips() == supply_; }
    long transactionCount() const { return txCount_; }

    // The journal feeds the on-disk ledger log; simulations can switch it off.
    void enableJournal(bool on) { journalOn_ = on; }
    std::vector<Transaction> takeJournal();

    // Friend function: reads private balances to print the audit report.
    friend void printAuditReport(std::ostream& os, const Ledger& ledger);

private:
    std::map<std::string, long> balances_;
    std::vector<Transaction> journal_;
    long supply_;
    long txCount_ = 0;
    bool journalOn_ = true;
};

void printAuditReport(std::ostream& os, const Ledger& ledger);

}  // namespace casino
