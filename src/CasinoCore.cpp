#include "CasinoCore.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <filesystem>

#include "Blackjack.hpp"
#include "Console.hpp"
#include "Exceptions.hpp"
#include "Poker.hpp"
#include "RedDog.hpp"

namespace casino {

const char* gameLabel(GameKind kind) {
    switch (kind) {
        case GameKind::Blackjack: return "Blackjack";
        case GameKind::RedDog: return "Red Dog";
        case GameKind::Poker: return "Poker";
    }
    return "?";
}

GameKind parseGameKind(const std::string& text) {
    std::string t;
    for (char c : text) {
        if (!std::isspace(static_cast<unsigned char>(c)) && c != '_' && c != '-') {
            t.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
    }
    if (t == "blackjack") return GameKind::Blackjack;
    if (t == "reddog") return GameKind::RedDog;
    if (t == "poker") return GameKind::Poker;
    throw InvalidInputException("Unknown game '" + text + "'");
}

CasinoCore::CasinoCore(const std::string& dataDir, std::ostream* out)
    : ctx_{ledger_, fairness_, out}, storage_(dataDir) {
    // Polymorphism: three different games stored behind one Game pointer type.
    for (GameKind kind : {GameKind::Blackjack, GameKind::RedDog, GameKind::Poker}) games_.push_back(makeGame(kind, ctx_));

    try {
        profiles_ = storage_.loadProfiles();
    } catch (const DataCorruptionException& e) {
        // Keep the damaged file for inspection and start with a clean slate.
        std::error_code ec;
        const std::string aside = storage_.profilesPath() + ".corrupt";
        std::filesystem::remove(aside, ec);
        std::filesystem::rename(storage_.profilesPath(), aside, ec);
        note(std::string(e.what()) + " The file was moved to '" + aside + "' and fresh profiles will be used.");
    } catch (const CasinoException& e) {
        note(std::string("Could not read saved players: ") + e.what());
    }
}

CasinoCore::~CasinoCore() {
    try {
        shutdown();
    } catch (...) {
        // destructors must not throw
    }
}

std::string CasinoCore::keyOf(const std::string& name) {
    std::string key;
    for (char c : name) key.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return key;
}

Game& CasinoCore::game(GameKind kind) { return *games_.at(static_cast<std::size_t>(kind)); }

std::string CasinoCore::enroll(Player& player) {
    const std::string key = keyOf(player.name());
    ledger_.openAccount(player.account());
    std::string message;

    if (enrolled_.count(key) == 0) {
        const auto saved = profiles_.find(key);
        if (saved == profiles_.end()) {
            ledger_.transfer(Ledger::HOUSE, player.account(), kWelcomeChips, "welcome chips");
            profiles_.emplace(key, PlayerProfile(player.name(), kWelcomeChips));
            message = "Welcome, " + player.name() + "! You start with " + console::money(kWelcomeChips) + " play chips.";
        } else {
            ledger_.transfer(Ledger::HOUSE, player.account(), saved->second.balance(), "restore saved balance");
            message = "Welcome back, " + player.name() + "! Your balance is " + console::money(saved->second.balance()) +
                      " chips.";
        }
        enrolled_.insert(key);
    }

    const long balance = ledger_.balance(player.account());
    if (balance < kTopUpFloor) {
        ledger_.transfer(Ledger::HOUSE, player.account(), kTopUpTo - balance, "play-money top-up");
        message += " You were low on chips, so the house topped you up to " + console::money(kTopUpTo) + ".";
    }
    syncBalances();
    return message;
}

bool CasinoCore::isEnrolled(const std::string& name) const { return enrolled_.count(keyOf(name)) != 0; }

long CasinoCore::balanceOf(const Player& player) const { return ledger_.balance(player.account()); }

long CasinoCore::balanceOf(const std::string& name) const { return ledger_.balance("player:" + keyOf(name)); }

void CasinoCore::syncBalances() {
    for (const std::string& key : enrolled_) {
        const auto it = profiles_.find(key);
        if (it != profiles_.end()) it->second.setBalance(ledger_.balance("player:" + key));
    }
}

std::unique_ptr<Game> CasinoCore::makeGame(GameKind kind, GameContext& ctx, const std::string& tableId) {
    switch (kind) {
        case GameKind::Blackjack: return std::make_unique<BlackjackGame>(ctx, tableId);
        case GameKind::RedDog: return std::make_unique<RedDogGame>(ctx, tableId);
        case GameKind::Poker: return std::make_unique<PokerGame>(ctx, tableId);
    }
    throw InvalidInputException("Unknown game kind");
}

RoundResult CasinoCore::playRound(GameKind kind, std::vector<Player*>& seated) {
    return playRound(game(kind), kind, seated);
}

RoundResult CasinoCore::playRound(Game& g, GameKind kind, std::vector<Player*>& seated) {
    const RoundResult result = g.playRound(seated);
    if (result.played) persistRound(kind, g.roundsPlayed(), result);
    return result;
}

void CasinoCore::persistRound(GameKind kind, int roundNumber, const RoundResult& result) {
    std::vector<HistoryRecord> history;
    const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
    for (const auto& entry : result.net) {
        const std::string key = keyOf(entry.first);
        auto profile = profiles_.find(key);
        if (profile == profiles_.end()) continue;
        profile->second.recordResult(entry.second);  // updates hands, wins, lifetime net
        HistoryRecord record;
        record.timestamp = now;
        record.round = roundNumber;
        record.game = static_cast<std::uint8_t>(kind);
        record.player = entry.first;
        record.net = entry.second;
        record.balanceAfter = ledger_.balance("player:" + key);
        history.push_back(record);
    }
    syncBalances();

    try {
        storage_.appendHistory(history);
        storage_.appendLedgerLog(ledger_.takeJournal());
        std::vector<FairnessService::Record> fresh;
        for (const auto& record : fairness_.records()) {
            if (record.revealed && loggedShuffles_.insert(record.id).second) fresh.push_back(record);
        }
        storage_.appendFairnessLog(fresh);
        storage_.saveProfiles(profiles_);
    } catch (const CasinoException& e) {
        note(std::string("Progress could not be saved: ") + e.what());
    }
}

Leaderboard<long> CasinoCore::balanceBoard() const {
    Leaderboard<long> board;
    for (const auto& entry : profiles_) {
        const std::string account = "player:" + entry.first;
        const long balance = ledger_.hasAccount(account) ? ledger_.balance(account) : entry.second.balance();
        board.set(entry.second.name(), balance);
    }
    return board;
}

Leaderboard<long> CasinoCore::winningsBoard() const {
    Leaderboard<long> board;
    for (const auto& entry : profiles_) board.set(entry.second.name(), entry.second.lifetimeNet());
    return board;
}

void CasinoCore::save() {
    syncBalances();
    try {
        storage_.saveProfiles(profiles_);
        storage_.appendLedgerLog(ledger_.takeJournal());
    } catch (const CasinoException& e) {
        note(std::string("Save failed: ") + e.what());
    }
}

void CasinoCore::shutdown() {
    if (closed_) return;
    closed_ = true;
    for (auto& g : games_) g->closeSession();
    fairness_.revealAll();
    std::vector<FairnessService::Record> fresh;
    for (const auto& record : fairness_.records()) {
        if (loggedShuffles_.insert(record.id).second) fresh.push_back(record);
    }
    try {
        storage_.appendFairnessLog(fresh);
    } catch (const CasinoException& e) {
        note(std::string("Could not write fairness log: ") + e.what());
    }
    save();
}

}  // namespace casino
