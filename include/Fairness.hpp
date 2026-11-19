// Fairness.hpp - provably fair shuffling (commit-reveal).
//
// Before a deck is shuffled the service picks a secret server seed and publishes
// its SHA-256 hash (the "commitment"). The shuffle is derived from the seed, so
// once the seed is revealed after the round anybody can recompute the shuffle
// and check that it matches the commitment and the deck order that was dealt.
#pragma once

#include <cstdint>
#include <deque>
#include <random>
#include <string>

#include "Deck.hpp"

namespace casino {

class FairnessService {
public:
    struct Record {
        long id = 0;
        std::string game;
        int round = 0;
        int numDecks = 1;
        std::string serverSeed;   // secret until `revealed` is true
        std::string commitment;   // sha256(serverSeed), published before the deal
        std::string orderHash;    // fingerprint of the deck order that was dealt
        bool revealed = false;
    };

    // Picks a secret seed and returns the record holding its public commitment.
    const Record& commit(const std::string& game, int round, int numDecks);
    // Shuffles `deck` using the most recent commitment.
    void shuffle(Deck& deck);
    // Marks a record as revealed (its seed may now be shown) and returns it.
    const Record& reveal(long id);
    // Reveals every record still hidden (used when the casino closes).
    void revealAll();

    // Recomputes the shuffle from the seed and compares with the stored hashes.
    static bool verify(const Record& record);
    static std::uint64_t deriveShuffleSeed(const Record& record);

    const std::deque<Record>& records() const { return records_; }
    long totalShuffles() const { return nextId_ - 1; }

private:
    static constexpr std::size_t kMaxStoredRecords = 500;

    std::string randomSeedHex();

    std::random_device entropy_;
    std::deque<Record> records_;
    long nextId_ = 1;
};

}  // namespace casino
