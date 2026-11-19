// Game.hpp - abstract base class for every table game
// (Unit II: abstract class, pure virtual functions, virtual destructor).
#pragma once

#include <iosfwd>
#include <map>
#include <string>
#include <vector>

#include "Fairness.hpp"
#include "Ledger.hpp"

namespace casino {

class Player;

// Everything a game needs from the outside world. `out == nullptr` runs the
// game silently, which the simulator and the tests rely on.
struct GameContext {
    Ledger& ledger;
    FairnessService& fairness;
    std::ostream* out;

    void say(const std::string& line) const;
};

// Outcome of one round, used for profiles, history and statistics.
struct RoundResult {
    std::map<std::string, long> net;  // player name -> chips won (+) or lost (-)
    long wagered = 0;                 // total chips risked by all players
    bool played = false;
};

class Game {
public:
    // `tableId` keeps the chips of different rooms apart ("" = the single default table).
    Game(GameContext& ctx, std::string name, std::string tableId = "");
    virtual ~Game();

    Game(const Game&) = delete;
    Game& operator=(const Game&) = delete;

    // Plays one full round with the given players. Pure virtual.
    virtual RoundResult playRound(std::vector<Player*>& seated) = 0;
    virtual std::string rules() const = 0;
    virtual long minBet() const = 0;
    virtual long maxBet() const = 0;
    virtual int minPlayers() const { return 1; }
    // Called when the casino closes so any open shuffle commitment can be revealed.
    virtual void closeSession() {}
    // Public (spectator-safe) snapshot of the table as JSON, for the web front end.
    // Hole cards that are still private are never included. "null" before the first round.
    virtual std::string tableJson() const { return table_; }

    const std::string& name() const { return name_; }
    int roundsPlayed() const { return rounds_; }

    // Static member: rounds played across every game object.
    static int totalRoundsAllGames() { return totalRounds_; }

protected:
    std::string tableAccount() const { return tableId_.empty() ? "table:" + name_ : "table:" + tableId_ + ":" + name_; }

    // Money movement helpers. Stakes sit in the table account until settled.
    void escrow(Player& player, long amount, const std::string& memo);
    void settleWin(Player& player, long stake, long profit, const std::string& memo);
    void settleLose(Player& player, long stake, const std::string& memo);
    void settlePush(Player& player, long stake, const std::string& memo);
    void sweepTable();  // any chips left in the table account go back to the house

    // Throws InvalidBetException when `bet` is outside [lo, hi].
    long checkedBet(long bet, long lo, long hi) const;
    void countRound();
    void setTable(std::string json) { table_ = std::move(json); }

    GameContext& ctx_;
    std::string name_;
    int rounds_ = 0;

private:
    std::string table_ = "null";
    std::string tableId_;
    static int totalRounds_;
};

}  // namespace casino
