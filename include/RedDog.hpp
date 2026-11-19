// RedDog.hpp - Red Dog (also called Acey-Deucey / In-Between).
//
// Every player antes. The dealer shows two cards; the spread is the number of
// ranks strictly between them. Players may raise (up to their ante), then a
// third card is dealt: if it falls strictly between the first two the player
// wins, otherwise loses. A fresh single deck is shuffled for every round.
#pragma once

#include "Game.hpp"
#include "Views.hpp"

namespace casino {

// Pays "N to 1" for a winning spread: 1 -> 5, 2 -> 4, 3 -> 2, 4 or more -> 1.
int redDogPayout(int spread);

class RedDogGame : public Game {
public:
    static constexpr long kMinBet = 10;
    static constexpr long kMaxBet = 500;
    static constexpr int kPairPayout = 11;  // three of a kind pays 11 to 1 on the ante

    explicit RedDogGame(GameContext& ctx, std::string tableId = "");

    RoundResult playRound(std::vector<Player*>& seated) override;
    std::string rules() const override;
    long minBet() const override { return kMinBet; }
    long maxBet() const override { return kMaxBet; }

private:
    struct Seat {
        Player* player;
        long stake;
        long raise = 0;
        std::string note;  // result text once settled
    };

    void publish();  // rebuilds the public table snapshot from the round in progress

    const std::vector<Seat>* liveSeats_ = nullptr;  // points at playRound()'s local seats while a round runs
    std::vector<std::string> cards_;                // "3S", "JD", then "??" or the third card
    int spread_ = -1;
    int payout_ = 0;
    std::string actor_;
};

}  // namespace casino
