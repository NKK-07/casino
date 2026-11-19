// Views.hpp - the read-only "views" games hand to players when asking for a
// decision, and the decisions players hand back. Games never talk to the
// console; players (human or bot) answer these structs.
#pragma once

#include <string>
#include <vector>

#include "Card.hpp"

namespace casino {

// --- betting (Blackjack and Red Dog ante) -----------------------------------
struct BetView {
    std::string game;
    long minBet = 0;
    long maxBet = 0;
    long balance = 0;
};

// --- Blackjack ---------------------------------------------------------------
enum class BjAction { Hit, Stand, Double, Split };

struct BlackjackView {
    std::vector<Card> cards;  // the hand being played
    Card dealerUp;
    long balance = 0;         // chips left in the wallet (after current stakes)
    long stake = 0;           // stake riding on this hand
    int handIndex = 0;        // 0-based; two hands after a split
    int handCount = 1;
    int total = 0;
    bool soft = false;
    bool canDouble = false;
    bool canSplit = false;

    explicit BlackjackView(Card up = Card(2, Suit::Clubs)) : dealerUp(up) {}
};

// --- Red Dog -------------------------------------------------------------------
struct RedDogView {
    Card low;
    Card high;
    int spread = 0;           // ranks strictly between the two cards
    long ante = 0;
    long maxRaise = 0;        // up to the ante, limited by the wallet
    int payout = 1;           // pays N to 1

    RedDogView() : low(2, Suit::Clubs), high(2, Suit::Clubs) {}
};

// --- Poker -----------------------------------------------------------------------
enum class Street { PreFlop, Flop, Turn, River };
enum class PokerKind { Fold, Check, Call, Raise };

struct PokerMove {
    PokerKind kind = PokerKind::Fold;
    long raiseTo = 0;  // total chips committed this betting round after raising
};

struct PokerView {
    std::vector<Card> hole;
    std::vector<Card> board;
    Street street = Street::PreFlop;
    long pot = 0;
    long toCall = 0;               // chips needed to stay in (capped at the stack)
    long stack = 0;                // chips behind this hand
    long committedThisRound = 0;
    long minRaiseTo = 0;
    long maxRaiseTo = 0;
    bool canRaise = false;
    long bigBlind = 0;
    int opponentsInHand = 0;
    std::vector<std::string> recentActions;  // lets a hot-seat player catch up
};

}  // namespace casino
