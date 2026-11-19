#include "ConsoleApp.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <iostream>

#include "Console.hpp"
#include "Exceptions.hpp"

namespace casino {
namespace {

std::string trimmed(const std::string& s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

}  // namespace

ConsoleApp::ConsoleApp(const std::string& dataDir) : core_(dataDir, &std::cout) {}

void ConsoleApp::printNotices() {
    for (const std::string& n : core_.notices()) std::cout << "  [!] " << n << '\n';
    core_.clearNotices();
}

std::vector<Player*> ConsoleApp::seats() {
    std::vector<Player*> out;
    for (auto& p : players_) out.push_back(p.get());
    return out;
}

void ConsoleApp::setupPlayers() {
    const long humans = console::readLong("How many people are playing at this keyboard (1-6)? ", 1, 6);
    for (long i = 0; i < humans; ++i) {
        while (true) {
            const std::string name = trimmed(console::readLine("  Name for player " + std::to_string(i + 1) + ": "));
            if (!isValidPlayerName(name)) {
                std::cout << "  ! Names are 2-16 letters, digits, '_' or '-'.\n";
                continue;
            }
            const bool taken = std::any_of(players_.begin(), players_.end(),
                                           [&](const auto& p) { return lower(p->name()) == lower(name); });
            if (taken) {
                std::cout << "  ! That name is already seated.\n";
                continue;
            }
            players_.push_back(std::make_unique<HumanPlayer>(name, humans > 1));
            break;
        }
    }

    const long maxBots = 6 - humans;
    long bots = 0;
    if (maxBots > 0) bots = console::readLong("Computer players to add (0-" + std::to_string(maxBots) + ")? ", 0, maxBots);
    static const char* const botNames[] = {"Bot_Ada", "Bot_Turing", "Bot_Hopper", "Bot_Knuth", "Bot_Lovelace"};
    static const BotPlayer::Style styles[] = {BotPlayer::Style::Steady, BotPlayer::Style::Tight,
                                              BotPlayer::Style::Aggressive, BotPlayer::Style::Steady,
                                              BotPlayer::Style::Tight};
    for (long i = 0; i < bots; ++i) {
        players_.push_back(std::make_unique<BotPlayer>(botNames[i], styles[i]));
    }

    std::cout << '\n';
    for (auto& p : players_) {
        const std::string message = core_.enroll(*p);
        if (!message.empty()) std::cout << "  " << message << '\n';
    }
}

void ConsoleApp::playGame(GameKind kind) {
    Game& game = core_.game(kind);
    if (static_cast<int>(players_.size()) < game.minPlayers()) {
        std::cout << "\n" << game.name() << " needs at least " << game.minPlayers()
                  << " players - restart and add a computer player.\n";
        return;
    }
    console::banner(game.name());
    std::cout << game.rules() << '\n';

    do {
        for (auto& p : players_) {  // keep the house promise: nobody is stuck with an empty stack
            const std::string message = core_.enroll(*p);
            if (!message.empty() && message.find("topped") != std::string::npos) std::cout << "  " << message << '\n';
        }
        console::rule();
        std::vector<Player*> seated = seats();
        const RoundResult result = core_.playRound(kind, seated);
        if (result.played) {
            std::cout << "\nRound summary:\n";
            for (const auto& entry : result.net) {
                std::cout << "  " << std::left << std::setw(16) << entry.first << std::right << std::setw(8)
                          << (entry.second >= 0 ? "+" : "") << entry.second << "   balance "
                          << console::money(core_.balanceOf(entry.first)) << '\n';
            }
        }
        printNotices();
    } while (console::readYesNo("\nPlay another round of " + game.name() + "? (y/n): "));
}

void ConsoleApp::showBalances() {
    console::banner("Balances and leaderboard");
    std::cout << "At this table:\n";
    for (auto& p : players_) {
        std::cout << "  " << std::left << std::setw(16) << p->name() << std::right << std::setw(10)
                  << console::money(core_.balanceOf(*p)) << " chips" << (p->isHuman() ? "" : "   (computer)") << '\n';
    }
    std::cout << "\nRichest players (all sessions):\n";
    int place = 1;
    for (const auto& entry : core_.balanceBoard().top(10)) {
        std::cout << "  " << place++ << ". " << std::left << std::setw(16) << entry.first << std::right << std::setw(10)
                  << console::money(entry.second) << '\n';
    }
    std::cout << "\nBiggest lifetime winners:\n";
    place = 1;
    for (const auto& entry : core_.winningsBoard().top(10)) {
        std::cout << "  " << place++ << ". " << std::left << std::setw(16) << entry.first << std::right << std::setw(10)
                  << (entry.second >= 0 ? "+" : "") << console::money(entry.second) << '\n';
    }
}

void ConsoleApp::showHistory() {
    console::banner("Recent hand history (data/history.bin)");
    const std::vector<HistoryRecord> all = core_.storage().loadHistory();
    if (all.empty()) {
        std::cout << "  No hands recorded yet.\n";
        return;
    }
    const std::size_t from = all.size() > 15 ? all.size() - 15 : 0;
    for (std::size_t i = from; i < all.size(); ++i) {
        const HistoryRecord& r = all[i];
        char when[32] = "";
        const std::time_t t = static_cast<std::time_t>(r.timestamp);
        std::strftime(when, sizeof when, "%Y-%m-%d %H:%M", std::localtime(&t));
        std::cout << "  " << when << "  " << std::left << std::setw(10) << historyGameName(r.game) << " #" << std::setw(4)
                  << r.round << std::setw(16) << r.player << std::right << std::setw(8) << (r.net >= 0 ? "+" : "")
                  << r.net << "  -> " << console::money(r.balanceAfter) << '\n';
    }
    std::cout << "  (" << all.size() << " records in total, every one checksum-verified)\n";
}

void ConsoleApp::showFairness() {
    console::banner("Fairness: commit-reveal shuffles");
    std::cout << "Each shuffle's hash is published BEFORE the deal; the seed is revealed after.\n"
              << "Anyone can recompute the shuffle from the seed to prove it was not changed.\n\n";
    const auto& records = core_.fairness().records();
    if (records.empty()) {
        std::cout << "  No shuffles yet - play a round first.\n";
        return;
    }
    const std::size_t from = records.size() > 10 ? records.size() - 10 : 0;
    for (std::size_t i = from; i < records.size(); ++i) {
        const auto& r = records[i];
        std::cout << "  #" << r.id << " " << r.game << " round " << r.round << "\n      commitment " << r.commitment.substr(0, 24)
                  << "...\n      ";
        if (r.revealed) {
            std::cout << "seed " << r.serverSeed.substr(0, 24) << "...  verification: "
                      << (FairnessService::verify(r) ? "PASSED" : "FAILED") << '\n';
        } else {
            std::cout << "seed hidden until the shoe ends\n";
        }
    }
}

void ConsoleApp::showRules() {
    console::banner("Rules");
    for (const auto& g : core_.games()) std::cout << g->rules() << "\n\n";
}

void ConsoleApp::mainMenu() {
    while (true) {
        console::banner("CASINO LOBBY");
        std::cout << "  1) Blackjack        5) Hand history (binary file)\n"
                  << "  2) Red Dog          6) Fairness - verify shuffles\n"
                  << "  3) Poker            7) Ledger audit\n"
                  << "  4) Balances         8) Rules\n"
                  << "  0) Save and quit\n";
        const long choice = console::readLong("Choose: ", 0, 8);
        try {
            switch (choice) {
                case 1: playGame(GameKind::Blackjack); break;
                case 2: playGame(GameKind::RedDog); break;
                case 3: playGame(GameKind::Poker); break;
                case 4: showBalances(); break;
                case 5: showHistory(); break;
                case 6: showFairness(); break;
                case 7:
                    console::banner("Ledger audit");
                    printAuditReport(std::cout, core_.ledger());
                    break;
                case 8: showRules(); break;
                default: return;
            }
        } catch (const InputClosedException&) {
            throw;  // handled in run()
        } catch (const InsufficientFundsException& e) {
            std::cout << "\n  ! " << e.what() << '\n';
        } catch (const CasinoException& e) {  // any other casino problem: report and carry on
            std::cout << "\n  ! " << e.what() << '\n';
        }
        printNotices();
    }
}

int ConsoleApp::run() {
    console::banner("WELCOME TO THE OOP CASINO  -  play money only");
    std::cout << "Blackjack, Red Dog and Texas Hold'em. Skill and probability, no real money.\n";
    printNotices();
    try {
        setupPlayers();
        mainMenu();
    } catch (const InputClosedException&) {
        std::cout << "\n\nInput closed - saving and leaving the casino.\n";
    } catch (const CasinoException& e) {
        std::cout << "\nFatal casino error: " << e.what() << '\n';
        core_.shutdown();
        return 1;
    }

    core_.shutdown();
    printNotices();
    std::cout << "\nSaved to '" << core_.storage().directory() << "'. Final balances:\n";
    for (auto& p : players_) {
        std::cout << "  " << std::left << std::setw(16) << p->name() << std::right << console::money(core_.balanceOf(*p))
                  << '\n';
    }
    std::cout << "Thanks for playing - see you next time!\n";
    return 0;
}

}  // namespace casino
