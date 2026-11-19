// main.cpp - entry point. Three ways to run the same casino engine:
//
//   casino                       terminal game (hot seat, with optional computer players)
//   casino --web [options]       web app: browsers join one shared table
//   casino --simulate <game> [rounds]   bot-only statistics run
#include <cstdlib>
#include <iostream>
#include <string>

#include "ConsoleApp.hpp"
#include "Exceptions.hpp"
#include "Simulator.hpp"
#include "WebApp.hpp"

namespace {

void printHelp() {
    std::cout << "OOP Casino - Blackjack, Red Dog and Texas Hold'em (play money)\n\n"
                 "Usage:\n"
                 "  casino                         play in the terminal\n"
                 "  casino --web                   start the web app on http://localhost:8080\n"
                 "      --port N                   use another port\n"
                 "      --host 0.0.0.0             let other devices on your network join\n"
                 "      --web-dir PATH             folder with the web files (default: web)\n"
                 "  casino --simulate GAME [N]     run N bot-only rounds (GAME: blackjack, reddog, poker)\n"
                 "  --data PATH                    folder for saved players and logs (default: data)\n"
                 "  casino --help\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::string dataDir = "data";
    casino::WebOptions web;
    bool webMode = false;
    std::string simulateGame;
    long simulateRounds = 10000;

    try {
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            const auto next = [&](const char* what) -> std::string {
                if (i + 1 >= argc) throw casino::InvalidInputException(std::string("Missing value after ") + what);
                return argv[++i];
            };
            if (arg == "--help" || arg == "-h") {
                printHelp();
                return 0;
            } else if (arg == "--web") {
                webMode = true;
            } else if (arg == "--port") {
                web.port = std::stoi(next("--port"));
            } else if (arg == "--host") {
                web.host = next("--host");
            } else if (arg == "--web-dir") {
                web.webDir = next("--web-dir");
            } else if (arg == "--data") {
                dataDir = next("--data");
            } else if (arg == "--simulate") {
                simulateGame = next("--simulate");
                if (i + 1 < argc && argv[i + 1][0] != '-') simulateRounds = std::stol(argv[++i]);
            } else {
                throw casino::InvalidInputException("Unknown option '" + arg + "' (try --help)");
            }
        }

        if (!simulateGame.empty()) return casino::runSimulation(simulateGame, simulateRounds, std::cout);
        if (webMode) {
            web.dataDir = dataDir;
            return casino::runWebServer(web);
        }
        casino::ConsoleApp app(dataDir);
        return app.run();
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 2;
    }
}
