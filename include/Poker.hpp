// Poker.hpp - No-limit Texas Hold'em with blinds, side pots and a small rake.
//
// Simplifications (documented in the README): chips behind are capped at a
// maximum buy-in per hand, a short all-in raise re-opens the betting, and there
// is no burn card. Rake is 3% of the pot (capped) and only taken once a flop is
// dealt ("no flop, no drop").
#pragma once

#include <list>
#include <vector>

#include "Deck.hpp"
#include "Game.hpp"
#include "PokerEval.hpp"
#include "Views.hpp"

namespace casino {

class PokerGame : public Game {
public:
    static constexpr long kSmallBlind = 5;
    static constexpr long kBigBlind = 10;
    static constexpr long kMaxStack = 1000;  // most chips a player can bring to one hand
    static constexpr int kRakePercent = 3;
    static constexpr long kRakeCap = 50;

    explicit PokerGame(GameContext& ctx, std::string tableId = "");

    RoundResult playRound(std::vector<Player*>& seated) override;
    std::string rules() const override;
    long minBet() const override { return kBigBlind; }
    long maxBet() const override { return kMaxStack; }
    int minPlayers() const override { return 2; }

private:
    struct Seat {
        Player* player = nullptr;
        long stack = 0;       // chips behind (not yet committed)
        long roundBet = 0;    // committed in the current betting round
        long totalBet = 0;    // committed in the whole hand
        long startBalance = 0;
        bool folded = false;
        bool allIn = false;
        std::vector<Card> hole;
        std::string note;      // last public action: "Called 20", "Folded", "Wins 240"
        std::string handText;  // revealed at showdown only
        bool shown = false;    // hole cards turned face up
    };

    void postBlind(std::size_t seat, long amount, const char* label);
    long put(Seat& seat, long amount);
    void bettingRound(Street street, std::size_t firstToAct);
    PokerView makeView(std::size_t seat) const;
    void dealBoard(int count);
    std::size_t liveCount() const;
    void announce(const std::string& line);
    void publish();  // rebuilds the public table snapshot
    void settleHand();
    std::string seatName(std::size_t seat) const;

    std::vector<Seat> seats_;
    std::vector<Card> board_;
    std::list<std::string> recentActions_;  // std::list: cheap pops from the front
    Deck deck_{1};
    Street street_ = Street::PreFlop;
    std::string actor_;  // player currently being asked to act (public)
    long pot_ = 0;
    long currentBet_ = 0;
    long lastRaise_ = kBigBlind;
    std::size_t button_ = 0;
    int buttonCounter_ = 0;
};

}  // namespace casino
