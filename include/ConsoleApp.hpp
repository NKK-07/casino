// ConsoleApp.hpp - the terminal front end: several people share one keyboard
// ("hot seat") and may be joined by computer players.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "CasinoCore.hpp"

namespace casino {

class ConsoleApp {
public:
    explicit ConsoleApp(const std::string& dataDir);

    // Runs until the user quits or input ends. Returns a process exit code.
    int run();

private:
    void printNotices();
    void setupPlayers();
    void mainMenu();
    void playGame(GameKind kind);
    void showBalances();
    void showHistory();
    void showFairness();
    void showRules();
    std::vector<Player*> seats();

    CasinoCore core_;
    std::vector<std::unique_ptr<Player>> players_;
};

}  // namespace casino
