// PokerEval.hpp - poker hand ranking, equity estimation and side pots.
#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <random>
#include <string>
#include <vector>

#include "Card.hpp"

namespace casino {

enum class HandCategory {
    HighCard,
    OnePair,
    TwoPair,
    ThreeOfAKind,
    Straight,
    Flush,
    FullHouse,
    FourOfAKind,
    StraightFlush
};

// A hand's strength packed into one integer: category in the high bits, then up
// to five tie-break ranks of four bits each. Comparing two HandRank values is a
// plain integer comparison, so all six relational operators are cheap.
class HandRank {
public:
    HandRank() = default;
    explicit HandRank(std::uint32_t score) : score_(score) {}

    HandCategory category() const { return static_cast<HandCategory>(score_ >> 20); }
    std::uint32_t score() const { return score_; }
    std::string describe() const;  // "Full house, Ks full of 4s"

    bool operator==(const HandRank& o) const { return score_ == o.score_; }
    bool operator!=(const HandRank& o) const { return score_ != o.score_; }
    bool operator<(const HandRank& o) const { return score_ < o.score_; }
    bool operator>(const HandRank& o) const { return score_ > o.score_; }
    bool operator<=(const HandRank& o) const { return score_ <= o.score_; }
    bool operator>=(const HandRank& o) const { return score_ >= o.score_; }

    friend std::ostream& operator<<(std::ostream& os, const HandRank& rank);

private:
    std::uint32_t score_ = 0;
};

// Rank exactly five cards.
HandRank evaluateFive(const Card* five);
// Best five-card hand out of 5, 6 or 7 cards.
HandRank evaluateBest(const std::vector<Card>& cards);

// Monte-Carlo estimate of the chance that `hole` wins (ties count as a share)
// against `opponents` random hands, completing the board at random.
double estimateEquity(const std::vector<Card>& hole, const std::vector<Card>& board, int opponents,
                      int trials, std::mt19937_64& rng);

// One player's total chips put into the pot during a hand.
struct Contribution {
    std::size_t seat;
    long total;
    bool folded;
};

struct SidePot {
    long amount = 0;
    std::vector<std::size_t> eligible;  // seats still in the hand that can win this pot
};

// Splits the pot into a main pot and side pots when players are all-in for
// different amounts. The pots always sum to the total contributed.
std::vector<SidePot> buildSidePots(const std::vector<Contribution>& contributions);

}  // namespace casino
