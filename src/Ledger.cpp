#include "Ledger.hpp"

#include <ostream>
#include <utility>

#include "Exceptions.hpp"

namespace casino {

const std::string Ledger::HOUSE = "HOUSE";
const std::string Ledger::RAKE = "RAKE";

std::ostream& operator<<(std::ostream& os, const Transaction& tx) {
    return os << '#' << tx.id << ' ' << tx.from << " -> " << tx.to << ' ' << tx.amount << " | " << tx.memo;
}

Ledger::Ledger(long houseBankroll) : supply_(houseBankroll) {
    balances_[HOUSE] = houseBankroll;
    balances_[RAKE] = 0;
}

void Ledger::openAccount(const std::string& id) { balances_.emplace(id, 0L); }

bool Ledger::hasAccount(const std::string& id) const { return balances_.count(id) != 0; }

long Ledger::balance(const std::string& id) const {
    const auto it = balances_.find(id);
    if (it == balances_.end()) throw UnknownAccountException(id);
    return it->second;
}

void Ledger::transfer(const std::string& from, const std::string& to, long amount, const std::string& memo) {
    if (amount < 0) throw InvalidBetException("transfer amount cannot be negative (" + std::to_string(amount) + ")");
    if (amount == 0) return;

    const auto source = balances_.find(from);
    if (source == balances_.end()) throw UnknownAccountException(from);
    const auto target = balances_.find(to);
    if (target == balances_.end()) throw UnknownAccountException(to);
    if (source->second < amount) throw InsufficientFundsException(from, amount, source->second);

    source->second -= amount;
    target->second += amount;
    ++txCount_;
    if (journalOn_) journal_.push_back(Transaction{txCount_, from, to, amount, memo});
}

long Ledger::totalChips() const {
    long total = 0;
    for (const auto& entry : balances_) total += entry.second;
    return total;
}

std::vector<Transaction> Ledger::takeJournal() {
    std::vector<Transaction> out;
    out.swap(journal_);
    return out;
}

void printAuditReport(std::ostream& os, const Ledger& ledger) {
    os << "Ledger audit\n";
    for (const auto& entry : ledger.balances_) {
        os << "  " << entry.first << ": " << entry.second << '\n';
    }
    os << "  Total chips : " << ledger.totalChips() << '\n';
    os << "  Chips issued: " << ledger.totalSupply() << '\n';
    os << "  Transactions: " << ledger.transactionCount() << '\n';
    os << (ledger.audit() ? "  RESULT: balanced - no chips created or lost.\n"
                          : "  RESULT: MISMATCH - ledger is inconsistent!\n");
}

}  // namespace casino
