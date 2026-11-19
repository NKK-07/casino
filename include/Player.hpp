// Player.hpp - abstract player with human and bot implementations
// (Unit II: inheritance, virtual functions, pure virtual functions, polymorphism).
//
// Games hold `Player*` and call the virtual decision functions; they neither
// know nor care whether a person or the computer answers.
#pragma once

#include <cstdint>
#include <random>
#include <string>

#include "Views.hpp"

namespace casino {

class Player {
public:
    explicit Player(std::string name);
    virtual ~Player();

    // Players own an identity, so copying one would be a bug.
    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;

    const std::string& name() const { return name_; }
    std::string account() const;  // ledger account id: "player:<lower-case name>"

    // Pure virtual interface implemented by HumanPlayer and BotPlayer.
    virtual bool isHuman() const = 0;
    virtual long chooseBet(const BetView& view) = 0;  // 0 = sit this round out
    virtual BjAction blackjackAction(const BlackjackView& view) = 0;
    virtual long redDogRaise(const RedDogView& view) = 0;
    virtual PokerMove pokerAction(const PokerView& view) = 0;

    // Static members track how many players exist.
    static int totalCreated() { return created_; }
    static int totalAlive() { return alive_; }

private:
    std::string name_;
    static int created_;
    static int alive_;
};

// A person at the keyboard. With more than one human the table is "hot seat":
// the screen is cleared so hole cards stay private.
class HumanPlayer : public Player {
public:
    HumanPlayer(std::string name, bool hotSeat) : Player(std::move(name)), hotSeat_(hotSeat) {}

    bool isHuman() const override { return true; }
    long chooseBet(const BetView& view) override;
    BjAction blackjackAction(const BlackjackView& view) override;
    long redDogRaise(const RedDogView& view) override;
    PokerMove pokerAction(const PokerView& view) override;

private:
    bool hotSeat_;
};

// A computer opponent. Blackjack follows basic strategy, Red Dog raises when the
// odds favour it, and Poker decisions compare Monte-Carlo equity with pot odds.
class BotPlayer : public Player {
public:
    enum class Style { Steady, Tight, Aggressive };

    BotPlayer(std::string name, Style style, std::uint64_t seed = std::random_device{}());

    bool isHuman() const override { return false; }
    long chooseBet(const BetView& view) override;
    BjAction blackjackAction(const BlackjackView& view) override;
    long redDogRaise(const RedDogView& view) override;
    PokerMove pokerAction(const PokerView& view) override;

    void setEquityTrials(int trials) { trials_ = trials; }
    Style style() const { return style_; }

private:
    Style style_;
    std::mt19937_64 rng_;
    int trials_ = 120;
};

}  // namespace casino
