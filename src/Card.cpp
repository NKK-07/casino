#include "Card.hpp"

#include <ostream>

#include "Exceptions.hpp"

namespace casino {

Card::Card(int rank, Suit suit) : rank_(rank), suit_(suit) {
    if (rank < kMinRank || rank > kAce) {
        throw InvalidInputException("Card rank must be between 2 and 14, got " + std::to_string(rank));
    }
}

std::string Card::rankName() const {
    switch (rank_) {
        case 11: return "J";
        case 12: return "Q";
        case 13: return "K";
        case 14: return "A";
        default: return std::to_string(rank_);
    }
}

char Card::suitLetter() const {
    switch (suit_) {
        case Suit::Clubs: return 'C';
        case Suit::Diamonds: return 'D';
        case Suit::Hearts: return 'H';
        case Suit::Spades: return 'S';
    }
    return '?';
}

std::string Card::str() const { return rankName() + std::string(1, suitLetter()); }

bool Card::operator<(const Card& other) const {
    if (rank_ != other.rank_) return rank_ < other.rank_;
    return static_cast<int>(suit_) < static_cast<int>(other.suit_);
}

std::ostream& operator<<(std::ostream& os, const Card& card) { return os << '[' << card.str() << ']'; }

}  // namespace casino
