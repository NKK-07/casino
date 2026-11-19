// CasinoCore.hpp - the casino itself, independent of any user interface.
//
// Owns the ledger, the fair shuffler, the three games and the saved player
// profiles. The console app and the web server are two front ends that drive
// the same object.
#pragma once

#include <iosfwd>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "Fairness.hpp"
#include "Game.hpp"
#include "Ledger.hpp"
#include "Player.hpp"
#include "Profile.hpp"
#include "Storage.hpp"
#include "Templates.hpp"

namespace casino {

enum class GameKind { Blackjack = 0, RedDog = 1, Poker = 2 };

const char* gameLabel(GameKind kind);
// Accepts "blackjack", "reddog", "red dog", "poker" (any case). Throws InvalidInputException.
GameKind parseGameKind(const std::string& text);

class CasinoCore {
public:
    static constexpr long kWelcomeChips = 1000;  // starting play-money balance
    static constexpr long kTopUpFloor = 100;     // below this a player is topped up on joining
    static constexpr long kTopUpTo = 500;

    // `out` receives the games' table talk (nullptr = silent). Saved data lives in `dataDir`.
    CasinoCore(const std::string& dataDir, std::ostream* out);
    ~CasinoCore();

    CasinoCore(const CasinoCore&) = delete;
    CasinoCore& operator=(const CasinoCore&) = delete;

    Ledger& ledger() { return ledger_; }
    const Ledger& ledger() const { return ledger_; }
    FairnessService& fairness() { return fairness_; }
    GameContext& context() { return ctx_; }
    Storage& storage() { return storage_; }

    Game& game(GameKind kind);
    const std::vector<std::unique_ptr<Game>>& games() const { return games_; }

    // Opens the player's ledger account and restores or creates their profile.
    // Returns a short message such as "Welcome! You start with 1,000 chips."
    std::string enroll(Player& player);
    bool isEnrolled(const std::string& name) const;
    long balanceOf(const Player& player) const;
    long balanceOf(const std::string& name) const;

    // Plays one round and then records results, history and logs on disk.
    RoundResult playRound(GameKind kind, std::vector<Player*>& seated);
    // Same, on a game owned by someone else (a web room has its own tables).
    RoundResult playRound(Game& game, GameKind kind, std::vector<Player*>& seated);

    // Factory: builds the game object for `kind` (polymorphism: callers only see Game).
    static std::unique_ptr<Game> makeGame(GameKind kind, GameContext& ctx, const std::string& tableId = "");

    Leaderboard<long> balanceBoard() const;
    Leaderboard<long> winningsBoard() const;
    const std::map<std::string, PlayerProfile>& profiles() const { return profiles_; }

    // Problems found while starting up or saving (corrupt file recovered, disk errors).
    const std::vector<std::string>& notices() const { return notices_; }
    void clearNotices() { notices_.clear(); }

    void save();      // write profiles (and logs) to disk
    void shutdown();  // reveal open shuffles, save everything; safe to call twice

private:
    static std::string keyOf(const std::string& name);
    void syncBalances();
    void persistRound(GameKind kind, int roundNumber, const RoundResult& result);
    void note(const std::string& text) { notices_.push_back(text); }

    Ledger ledger_;
    FairnessService fairness_;
    GameContext ctx_;
    Storage storage_;
    std::vector<std::unique_ptr<Game>> games_;
    std::map<std::string, PlayerProfile> profiles_;  // keyed by lower-case name
    std::set<std::string> enrolled_;                 // lower-case names active this session
    std::set<long> loggedShuffles_;
    std::vector<std::string> notices_;
    bool closed_ = false;
};

}  // namespace casino
