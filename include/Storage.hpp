// Storage.hpp - persistence (Unit IV: text and binary file handling).
//
//   data/players.txt   text    player profiles + SHA-256 checksum line
//   data/history.bin   binary  one fixed-size, checksummed record per player per round
//   data/ledger.log    text    append-only journal of every chip transfer
//   data/fairness.log  text    revealed shuffle seeds so any deal can be re-verified
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "Fairness.hpp"
#include "Ledger.hpp"
#include "Profile.hpp"

namespace casino {

struct HistoryRecord {
    std::int64_t timestamp = 0;   // seconds since the Unix epoch
    std::int32_t round = 0;       // that game's round counter
    std::uint8_t game = 0;        // 0 Blackjack, 1 Red Dog, 2 Poker
    std::string player;           // up to 23 characters
    std::int64_t net = 0;         // chips won or lost
    std::int64_t balanceAfter = 0;
};

const char* historyGameName(std::uint8_t game);

class Storage {
public:
    // Creates the directory if needed. Throws FileAccessException on failure.
    explicit Storage(std::string directory = "data");

    // Profiles. load returns an empty map when no file exists yet and throws
    // DataCorruptionException when the checksum or contents are bad.
    std::map<std::string, PlayerProfile> loadProfiles() const;
    void saveProfiles(const std::map<std::string, PlayerProfile>& profiles) const;

    // Binary history.
    void appendHistory(const std::vector<HistoryRecord>& records) const;
    std::vector<HistoryRecord> loadHistory() const;

    // Text logs.
    void appendLedgerLog(const std::vector<Transaction>& transactions) const;
    void appendFairnessLog(const std::vector<FairnessService::Record>& records) const;

    const std::string& directory() const { return directory_; }
    std::string profilesPath() const { return directory_ + "/players.txt"; }
    std::string historyPath() const { return directory_ + "/history.bin"; }
    std::string ledgerPath() const { return directory_ + "/ledger.log"; }
    std::string fairnessPath() const { return directory_ + "/fairness.log"; }

    static constexpr std::size_t kRecordSize = 57;

private:
    std::string directory_;
};

}  // namespace casino
