#include "Simulator.hpp"

#include <algorithm>
#include <iomanip>
#include <memory>
#include <ostream>

#include "Blackjack.hpp"
#include "Exceptions.hpp"
#include "Game.hpp"
#include "Player.hpp"
#include "Poker.hpp"
#include "RedDog.hpp"

namespace casino {

int runSimulation(const std::string& which, long rounds, std::ostream& out) {
    Ledger ledger(1000000000L);
    ledger.enableJournal(false);  // millions of transactions: do not keep them in memory
    FairnessService fairness;
    GameContext ctx{ledger, fairness, nullptr};

    std::unique_ptr<Game> game;
    if (which == "blackjack") game = std::make_unique<BlackjackGame>(ctx);
    else if (which == "reddog") game = std::make_unique<RedDogGame>(ctx);
    else if (which == "poker") game = std::make_unique<PokerGame>(ctx);
    else throw InvalidInputException("Unknown game '" + which + "' (use blackjack, reddog or poker)");

    const bool poker = which == "poker";
    const BotPlayer::Style styles[5] = {BotPlayer::Style::Steady, BotPlayer::Style::Tight, BotPlayer::Style::Aggressive,
                                        BotPlayer::Style::Steady, BotPlayer::Style::Tight};
    const char* labels[5] = {"Steady-1", "Tight-1", "Aggro-1", "Steady-2", "Tight-2"};
    const std::size_t seatCount = poker ? 5 : 4;

    std::vector<std::unique_ptr<BotPlayer>> bots;
    std::vector<Player*> seated;
    std::vector<long> nets(seatCount, 0);
    for (std::size_t i = 0; i < seatCount; ++i) {
        // Blackjack and Red Dog bets are flat (min bet) so the edge is measured per unit staked.
        const BotPlayer::Style style = poker ? styles[i] : BotPlayer::Style::Steady;
        bots.push_back(std::make_unique<BotPlayer>(labels[i], style, 1000 + i));
        bots.back()->setEquityTrials(60);
        ledger.openAccount(bots.back()->account());
        seated.push_back(bots.back().get());
    }

    const long kBankroll = 100000;
    long totalWagered = 0;
    long totalNet = 0;
    long handsPlayed = 0;
    for (long r = 0; r < rounds; ++r) {
        for (auto& b : bots) {  // keep everyone solvent so every round is played by every bot
            const long bal = ledger.balance(b->account());
            if (bal < kBankroll / 2) ledger.transfer(Ledger::HOUSE, b->account(), kBankroll - bal, "sim top-up");
        }
        const RoundResult result = game->playRound(seated);
        if (!result.played) continue;
        ++handsPlayed;
        totalWagered += result.wagered;
        for (std::size_t i = 0; i < seatCount; ++i) {
            const auto it = result.net.find(bots[i]->name());
            if (it != result.net.end()) {
                nets[i] += it->second;
                totalNet += it->second;
            }
        }
    }
    game->closeSession();
    fairness.revealAll();

    std::size_t verified = 0;
    for (const auto& record : fairness.records()) {
        if (FairnessService::verify(record)) ++verified;
    }

    out << std::fixed << std::setprecision(3);
    out << "Simulation: " << game->name() << ", " << handsPlayed << " rounds, " << seatCount << " bot players\n";
    out << "  Total wagered       : " << totalWagered << " chips\n";
    if (poker) {
        out << "  Rake collected      : " << ledger.balance(Ledger::RAKE) << " chips ("
            << (totalWagered ? 100.0 * ledger.balance(Ledger::RAKE) / totalWagered : 0.0) << "% of chips put in)\n";
        for (std::size_t i = 0; i < seatCount; ++i) {
            out << "  " << std::left << std::setw(9) << labels[i] << " net " << std::right << std::setw(9) << nets[i]
                << " chips (" << (handsPlayed ? static_cast<double>(nets[i]) / handsPlayed : 0.0)
                << " per hand)\n";
        }
    } else {
        const double edge = totalWagered ? -100.0 * totalNet / totalWagered : 0.0;
        out << "  Players' net result : " << totalNet << " chips\n";
        out << "  Measured house edge : " << edge << "% of every chip wagered\n";
    }
    out << "  Shuffles checked    : " << verified << " of " << fairness.records().size() << " stored records verified\n";
    const bool audit = ledger.audit();
    out << "  Ledger audit        : " << (audit ? "balanced" : "MISMATCH") << " (" << ledger.transactionCount()
        << " transactions)\n";
    return (audit && verified == fairness.records().size()) ? 0 : 1;
}

}  // namespace casino
