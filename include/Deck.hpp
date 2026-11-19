// Deck.hpp - one or more 52-card decks (Unit I: classes, STL vector).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "Card.hpp"

namespace casino {

class Deck {
public:
    explicit Deck(int numDecks = 1);

    // Rebuilds the cards in factory order and rewinds the dealing position.
    void reset();
    // Deterministic Fisher-Yates shuffle driven by a 64-bit seed (see FairnessService).
    void shuffle(std::uint64_t seed);
    // Throws EmptyDeckException when no cards are left.
    Card deal();

    std::size_t remaining() const { return cards_.size() - next_; }
    std::size_t size() const { return cards_.size(); }
    int numDecks() const { return numDecks_; }
    const std::vector<Card>& cards() const { return cards_; }

    // SHA-256 of the full card order; published so a shuffle can be re-checked later.
    std::string orderFingerprint() const;

private:
    int numDecks_;
    std::vector<Card> cards_;
    std::size_t next_ = 0;
};

}  // namespace casino
