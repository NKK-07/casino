#include "Game.hpp"

#include <ostream>

#include "Exceptions.hpp"
#include "Player.hpp"

namespace casino {

int Game::totalRounds_ = 0;

void GameContext::say(const std::string& line) const {
    if (out != nullptr) *out << line << '\n';
}

Game::Game(GameContext& ctx, std::string name, std::string tableId)
    : ctx_(ctx), name_(std::move(name)), tableId_(std::move(tableId)) {
    ctx_.ledger.openAccount(tableAccount());
}

Game::~Game() = default;

void Game::countRound() {
    ++rounds_;
    ++totalRounds_;
}

void Game::escrow(Player& player, long amount, const std::string& memo) {
    ctx_.ledger.transfer(player.account(), tableAccount(), amount, name_ + ": " + memo + " (" + player.name() + ")");
}

void Game::settleWin(Player& player, long stake, long profit, const std::string& memo) {
    ctx_.ledger.transfer(tableAccount(), player.account(), stake, name_ + ": stake returned (" + player.name() + ")");
    ctx_.ledger.transfer(Ledger::HOUSE, player.account(), profit, name_ + ": " + memo + " (" + player.name() + ")");
}

void Game::settleLose(Player& player, long stake, const std::string& memo) {
    ctx_.ledger.transfer(tableAccount(), Ledger::HOUSE, stake, name_ + ": " + memo + " (" + player.name() + ")");
}

void Game::settlePush(Player& player, long stake, const std::string& memo) {
    ctx_.ledger.transfer(tableAccount(), player.account(), stake, name_ + ": " + memo + " (" + player.name() + ")");
}

void Game::sweepTable() {
    const long left = ctx_.ledger.balance(tableAccount());
    if (left > 0) ctx_.ledger.transfer(tableAccount(), Ledger::HOUSE, left, name_ + ": sweep");
}

long Game::checkedBet(long bet, long lo, long hi) const {
    if (bet < lo || bet > hi) {
        throw InvalidBetException(std::to_string(bet) + " is outside the allowed range " + std::to_string(lo) + "-" +
                                  std::to_string(hi));
    }
    return bet;
}

}  // namespace casino
