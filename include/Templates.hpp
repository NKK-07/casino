// Templates.hpp - reusable generic helpers (Unit III: function and class templates).
#pragma once

#include <algorithm>
#include <cstddef>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace casino {

// Function template: joins any container whose elements can be streamed.
// Works for vector<Card>, list<string>, set<int>, ...
template <typename Container>
std::string joinItems(const Container& items, const std::string& separator = " ") {
    std::ostringstream os;
    bool first = true;
    for (const auto& item : items) {
        if (!first) os << separator;
        os << item;
        first = false;
    }
    return os.str();
}

// Function template: largest element of a non-empty vector.
template <typename T>
const T& maxOf(const std::vector<T>& values) {
    if (values.empty()) throw std::invalid_argument("maxOf called on an empty vector");
    return *std::max_element(values.begin(), values.end());
}

// Function template: restrict a value to [lo, hi].
template <typename T>
T clampValue(T value, T lo, T hi) {
    return value < lo ? lo : (value > hi ? hi : value);
}

// Class template: a ranking of named entries by any comparable score.
// Instantiated as Leaderboard<long> for balances and lifetime winnings.
template <typename Score>
class Leaderboard {
public:
    using Entry = std::pair<std::string, Score>;

    void set(const std::string& name, Score score) {
        for (auto& entry : entries_) {
            if (entry.first == name) {
                entry.second = score;
                return;
            }
        }
        entries_.emplace_back(name, score);
    }

    // Highest scores first; ties are broken alphabetically so output is stable.
    std::vector<Entry> top(std::size_t count) const {
        std::vector<Entry> sorted = entries_;
        std::sort(sorted.begin(), sorted.end(), [](const Entry& a, const Entry& b) {
            if (a.second != b.second) return a.second > b.second;
            return a.first < b.first;
        });
        if (sorted.size() > count) sorted.resize(count);
        return sorted;
    }

    std::size_t size() const { return entries_.size(); }

private:
    std::vector<Entry> entries_;
};

}  // namespace casino
