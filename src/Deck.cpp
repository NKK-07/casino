#include "Deck.hpp"

#include <random>
#include <utility>

#include "Exceptions.hpp"
#include "Sha256.hpp"
#include "Templates.hpp"

namespace casino {
namespace {

// Unbiased random number in [0, bound) using rejection sampling.
std::uint64_t boundedRandom(std::mt19937_64& engine, std::uint64_t bound) {
    const std::uint64_t limit = UINT64_MAX - (UINT64_MAX % bound);
    std::uint64_t value;
    do {
        value = engine();
    } while (value >= limit);
    return value % bound;
}

}  // namespace

Deck::Deck(int numDecks) : numDecks_(numDecks) {
    if (numDecks < 1) throw InvalidInputException("A deck needs at least one 52-card set");
    reset();
}

void Deck::reset() {
    cards_.clear();
    cards_.reserve(static_cast<std::size_t>(numDecks_) * 52);
    for (int d = 0; d < numDecks_; ++d) {
        for (int s = 0; s < 4; ++s) {
            for (int r = Card::kMinRank; r <= Card::kAce; ++r) {
                cards_.emplace_back(r, static_cast<Suit>(s));
            }
        }
    }
    next_ = 0;
}

void Deck::shuffle(std::uint64_t seed) {
    std::mt19937_64 engine(seed);
    for (std::size_t i = cards_.size() - 1; i > 0; --i) {
        const std::size_t j = static_cast<std::size_t>(boundedRandom(engine, i + 1));
        std::swap(cards_[i], cards_[j]);
    }
    next_ = 0;
}

Card Deck::deal() {
    if (next_ >= cards_.size()) throw EmptyDeckException();
    return cards_[next_++];
}

std::string Deck::orderFingerprint() const {
    std::vector<std::string> names;
    names.reserve(cards_.size());
    for (const Card& c : cards_) names.push_back(c.str());
    return sha256Hex(joinItems(names, ","));
}

}  // namespace casino
