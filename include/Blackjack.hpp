// Blackjack.hpp - multi-seat Blackjack against the dealer.
//
// House rules: 6-deck shoe, dealer stands on all 17s, blackjack pays 3:2,
// double on any first two cards (also after a split), one split per round,
// split aces receive one card each, no insurance or surrender.
#pragma once

#include <vector>

#include "Deck.hpp"
#include "Game.hpp"
#include "Views.hpp"

namespace casino {

struct BjValue {
    int total;
    bool soft;  // an ace is currently counted as 11
};

int blackjackCardValue(const Card& card);              // 2-10, face cards 10, ace 11
BjValue blackjackValue(const std::vector<Card>& hand);  // best total without busting
// Basic strategy for these house rules; the computer players play with it.
BjAction basicStrategy(const BlackjackView& view);

class BlackjackGame : public Game {
public:
    static constexpr int kDecks = 6;
    static constexpr std::size_t kCutCard = 100;  // reshuffle when fewer cards remain
    static constexpr long kMinBet = 10;
    static constexpr long kMaxBet = 1000;

    explicit BlackjackGame(GameContext& ctx, std::string tableId = "");

    RoundResult playRound(std::vector<Player*>& seated) override;
    std::string rules() const override;
    long minBet() const override { return kMinBet; }
    long maxBet() const override { return kMaxBet; }
    void closeSession() override;

private:
    struct Hand {
        std::vector<Card> cards;
        long stake = 0;
        bool fromSplit = false;
        bool splitAce = false;
        std::string note;  // result text once settled
    };
    struct Seat {
        Player* player = nullptr;
        std::vector<Hand> hands;
    };

    void prepareShoe();
    void publish();  // rebuilds the public table snapshot from the round in progress
    Card draw();
    void playSeat(Seat& seat, const Card& dealerUp);
    static std::string describe(const std::vector<Card>& cards);

    // Pointers into playRound()'s locals, valid only while a round is running.
    const std::vector<Seat>* liveSeats_ = nullptr;
    const std::vector<Card>* liveDealer_ = nullptr;
    bool holeHidden_ = false;
    std::string actor_;

    Deck shoe_{kDecks};
    bool shoeReady_ = false;
    long shoeRecordId_ = 0;
    int shoeNumber_ = 0;
};

}  // namespace casino
