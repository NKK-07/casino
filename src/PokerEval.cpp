#include "PokerEval.hpp"

#include <algorithm>
#include <ostream>

#include "Exceptions.hpp"

namespace casino {
namespace {

std::string pluralRank(int rank) {
    // "Ks", "10s", "As" - used when describing pairs, trips and full houses.
    return Card(rank, Suit::Spades).rankName() + "s";
}

std::string singleRank(int rank) { return Card(rank, Suit::Spades).rankName(); }

std::uint32_t pack(HandCategory category, const int (&tiebreak)[5]) {
    std::uint32_t score = static_cast<std::uint32_t>(category) << 20;
    for (int i = 0; i < 5; ++i) {
        score |= static_cast<std::uint32_t>(tiebreak[i] & 0xF) << (16 - 4 * i);
    }
    return score;
}

}  // namespace

std::string HandRank::describe() const {
    const int t0 = (score_ >> 16) & 0xF;
    const int t1 = (score_ >> 12) & 0xF;
    switch (category()) {
        case HandCategory::HighCard: return "High card (" + singleRank(t0) + ")";
        case HandCategory::OnePair: return "Pair of " + pluralRank(t0);
        case HandCategory::TwoPair: return "Two pair (" + pluralRank(t0) + " and " + pluralRank(t1) + ")";
        case HandCategory::ThreeOfAKind: return "Three of a kind (" + pluralRank(t0) + ")";
        case HandCategory::Straight: return "Straight (" + singleRank(t0) + " high)";
        case HandCategory::Flush: return "Flush (" + singleRank(t0) + " high)";
        case HandCategory::FullHouse: return "Full house (" + pluralRank(t0) + " full of " + pluralRank(t1) + ")";
        case HandCategory::FourOfAKind: return "Four of a kind (" + pluralRank(t0) + ")";
        case HandCategory::StraightFlush:
            return t0 == Card::kAce ? "Royal flush" : "Straight flush (" + singleRank(t0) + " high)";
    }
    return "Unknown";
}

std::ostream& operator<<(std::ostream& os, const HandRank& rank) { return os << rank.describe(); }

HandRank evaluateFive(const Card* five) {
    int count[15] = {0};
    bool flush = true;
    for (int i = 0; i < 5; ++i) {
        ++count[five[i].rank()];
        if (five[i].suit() != five[0].suit()) flush = false;
    }

    // Groups ordered by (size desc, rank desc): quads/trips/pairs first, then kickers.
    int groupRank[5] = {0};
    int groupSize[5] = {0};
    int groups = 0;
    for (int size = 4; size >= 1; --size) {
        for (int r = Card::kAce; r >= Card::kMinRank; --r) {
            if (count[r] == size) {
                groupRank[groups] = r;
                groupSize[groups] = size;
                ++groups;
            }
        }
    }

    bool straight = false;
    int straightHigh = 0;
    if (groups == 5) {
        if (groupRank[0] - groupRank[4] == 4) {
            straight = true;
            straightHigh = groupRank[0];
        } else if (groupRank[0] == Card::kAce && groupRank[1] == 5 && groupRank[2] == 4 &&
                   groupRank[3] == 3 && groupRank[4] == 2) {
            straight = true;  // the "wheel": A-2-3-4-5, ace plays low
            straightHigh = 5;
        }
    }

    int tiebreak[5] = {0, 0, 0, 0, 0};
    for (int i = 0; i < groups; ++i) tiebreak[i] = groupRank[i];

    if (straight && flush) {
        const int high[5] = {straightHigh, 0, 0, 0, 0};
        return HandRank(pack(HandCategory::StraightFlush, high));
    }
    if (groupSize[0] == 4) return HandRank(pack(HandCategory::FourOfAKind, tiebreak));
    if (groupSize[0] == 3 && groupSize[1] == 2) return HandRank(pack(HandCategory::FullHouse, tiebreak));
    if (flush) return HandRank(pack(HandCategory::Flush, tiebreak));
    if (straight) {
        const int high[5] = {straightHigh, 0, 0, 0, 0};
        return HandRank(pack(HandCategory::Straight, high));
    }
    if (groupSize[0] == 3) return HandRank(pack(HandCategory::ThreeOfAKind, tiebreak));
    if (groupSize[0] == 2 && groupSize[1] == 2) return HandRank(pack(HandCategory::TwoPair, tiebreak));
    if (groupSize[0] == 2) return HandRank(pack(HandCategory::OnePair, tiebreak));
    return HandRank(pack(HandCategory::HighCard, tiebreak));
}

HandRank evaluateBest(const std::vector<Card>& cards) {
    const int n = static_cast<int>(cards.size());
    if (n < 5 || n > 7) throw InvalidInputException("Poker evaluation needs 5 to 7 cards");

    HandRank best;
    bool haveBest = false;
    Card five[5] = {cards[0], cards[0], cards[0], cards[0], cards[0]};
    for (int mask = 0; mask < (1 << n); ++mask) {
        int bits = 0;
        for (int i = 0; i < n; ++i) bits += (mask >> i) & 1;
        if (bits != 5) continue;
        int k = 0;
        for (int i = 0; i < n; ++i) {
            if ((mask >> i) & 1) five[k++] = cards[static_cast<std::size_t>(i)];
        }
        const HandRank rank = evaluateFive(five);
        if (!haveBest || rank > best) {
            best = rank;
            haveBest = true;
        }
    }
    return best;
}

double estimateEquity(const std::vector<Card>& hole, const std::vector<Card>& board, int opponents,
                      int trials, std::mt19937_64& rng) {
    if (hole.size() != 2 || board.size() > 5 || opponents < 1 || trials < 1) {
        throw InvalidInputException("estimateEquity: bad arguments");
    }

    std::vector<Card> remaining;
    remaining.reserve(52);
    for (int s = 0; s < 4; ++s) {
        for (int r = Card::kMinRank; r <= Card::kAce; ++r) {
            const Card c(r, static_cast<Suit>(s));
            if (std::find(hole.begin(), hole.end(), c) != hole.end()) continue;
            if (std::find(board.begin(), board.end(), c) != board.end()) continue;
            remaining.push_back(c);
        }
    }

    const std::size_t boardNeeded = 5 - board.size();
    // Never ask for more cards than the deck still has.
    while (boardNeeded + 2 * static_cast<std::size_t>(opponents) > remaining.size()) --opponents;
    const std::size_t draws = boardNeeded + 2 * static_cast<std::size_t>(opponents);

    std::vector<Card> mine, theirs;
    mine.reserve(7);
    theirs.reserve(7);
    double wins = 0.0;

    for (int t = 0; t < trials; ++t) {
        for (std::size_t i = 0; i < draws; ++i) {  // partial Fisher-Yates
            const std::size_t j = i + static_cast<std::size_t>(rng() % (remaining.size() - i));
            std::swap(remaining[i], remaining[j]);
        }
        mine.assign(hole.begin(), hole.end());
        mine.insert(mine.end(), board.begin(), board.end());
        mine.insert(mine.end(), remaining.begin(), remaining.begin() + static_cast<long>(boardNeeded));
        const HandRank myRank = evaluateBest(mine);

        bool beaten = false;
        int ties = 0;
        for (int o = 0; o < opponents && !beaten; ++o) {
            theirs.assign(board.begin(), board.end());
            theirs.insert(theirs.end(), remaining.begin(), remaining.begin() + static_cast<long>(boardNeeded));
            theirs.push_back(remaining[boardNeeded + 2 * static_cast<std::size_t>(o)]);
            theirs.push_back(remaining[boardNeeded + 2 * static_cast<std::size_t>(o) + 1]);
            const HandRank theirRank = evaluateBest(theirs);
            if (theirRank > myRank) beaten = true;
            else if (theirRank == myRank) ++ties;
        }
        if (!beaten) wins += 1.0 / (ties + 1);
    }
    return wins / trials;
}

std::vector<SidePot> buildSidePots(const std::vector<Contribution>& contributions) {
    std::vector<long> levels;
    for (const Contribution& c : contributions) {
        if (!c.folded && c.total > 0) levels.push_back(c.total);
    }
    std::sort(levels.begin(), levels.end());
    levels.erase(std::unique(levels.begin(), levels.end()), levels.end());

    std::vector<SidePot> pots;
    long previous = 0;
    for (long level : levels) {
        SidePot pot;
        for (const Contribution& c : contributions) {
            const long slice = std::min(c.total, level) - previous;
            if (slice > 0) pot.amount += slice;
            if (!c.folded && c.total >= level) pot.eligible.push_back(c.seat);
        }
        if (pot.amount > 0) pots.push_back(pot);
        previous = level;
    }

    // Chips above the highest live player's level (cannot normally happen) join the last pot.
    long extra = 0;
    for (const Contribution& c : contributions) {
        if (c.total > previous) extra += c.total - previous;
    }
    if (extra > 0 && !pots.empty()) pots.back().amount += extra;
    return pots;
}

}  // namespace casino
