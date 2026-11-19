// Card.hpp - a playing card (Unit I: classes, Unit III: operator overloading).
#pragma once

#include <iosfwd>
#include <string>

namespace casino {

enum class Suit { Clubs, Diamonds, Hearts, Spades };

class Card {
public:
    static constexpr int kMinRank = 2;
    static constexpr int kAce = 14;  // aces are stored high; games treat them low where needed

    // Throws InvalidInputException when rank is outside 2..14.
    Card(int rank, Suit suit);

    int rank() const { return rank_; }
    Suit suit() const { return suit_; }
    bool isAce() const { return rank_ == kAce; }

    std::string rankName() const;  // "2".."10", "J", "Q", "K", "A"
    char suitLetter() const;       // 'C', 'D', 'H', 'S'
    std::string str() const;       // e.g. "AS", "10H"

    // Operator overloading: equality and ordering (rank first, then suit).
    bool operator==(const Card& other) const { return rank_ == other.rank_ && suit_ == other.suit_; }
    bool operator!=(const Card& other) const { return !(*this == other); }
    bool operator<(const Card& other) const;

    // Friend function: prints a card as "[AS]".
    friend std::ostream& operator<<(std::ostream& os, const Card& card);

private:
    int rank_;
    Suit suit_;
};

}  // namespace casino
