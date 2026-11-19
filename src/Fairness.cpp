#include "Fairness.hpp"

#include <iomanip>
#include <sstream>

#include "Exceptions.hpp"
#include "Sha256.hpp"

namespace casino {

std::string FairnessService::randomSeedHex() {
    std::ostringstream os;
    os << std::hex << std::setfill('0');
    for (int i = 0; i < 8; ++i) os << std::setw(8) << static_cast<std::uint32_t>(entropy_());
    return os.str();  // 64 hex characters = 256 bits
}

std::uint64_t FairnessService::deriveShuffleSeed(const Record& record) {
    const std::string material = record.serverSeed + ":" + record.game + ":" + std::to_string(record.round);
    return digestToSeed(sha256(material));
}

const FairnessService::Record& FairnessService::commit(const std::string& game, int round, int numDecks) {
    Record record;
    record.id = nextId_++;
    record.game = game;
    record.round = round;
    record.numDecks = numDecks;
    record.serverSeed = randomSeedHex();
    record.commitment = sha256Hex(record.serverSeed);
    records_.push_back(record);
    if (records_.size() > kMaxStoredRecords) records_.pop_front();
    return records_.back();
}

void FairnessService::shuffle(Deck& deck) {
    if (records_.empty()) throw CasinoException("Shuffle requested before any commitment was made");
    Record& record = records_.back();
    if (deck.numDecks() != record.numDecks) {
        throw CasinoException("Deck size does not match the committed shuffle");
    }
    deck.reset();
    deck.shuffle(deriveShuffleSeed(record));
    record.orderHash = deck.orderFingerprint();
}

const FairnessService::Record& FairnessService::reveal(long id) {
    if (records_.empty() || id < records_.front().id || id > records_.back().id) {
        throw CasinoException("Shuffle record #" + std::to_string(id) + " is no longer stored");
    }
    Record& record = records_[static_cast<std::size_t>(id - records_.front().id)];
    record.revealed = true;
    return record;
}

void FairnessService::revealAll() {
    for (Record& record : records_) record.revealed = true;
}

bool FairnessService::verify(const Record& record) {
    if (sha256Hex(record.serverSeed) != record.commitment) return false;
    Deck deck(record.numDecks);
    deck.shuffle(deriveShuffleSeed(record));
    return deck.orderFingerprint() == record.orderHash;
}

}  // namespace casino
