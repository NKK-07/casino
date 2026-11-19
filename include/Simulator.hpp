// Simulator.hpp - runs bot-only games silently and reports statistics.
// Used to confirm each game's measured house edge / rake matches its design.
#pragma once

#include <iosfwd>
#include <string>

namespace casino {

// `game` is "blackjack", "reddog" or "poker". Returns 0 on success, non-zero
// when the ledger audit or a fairness check fails.
int runSimulation(const std::string& game, long rounds, std::ostream& out);

}  // namespace casino
