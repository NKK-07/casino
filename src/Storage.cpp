#include "Storage.hpp"

#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "Exceptions.hpp"
#include "Sha256.hpp"

namespace fs = std::filesystem;

namespace casino {
namespace {

const char kMagic[8] = {'C', 'A', 'S', 'H', 'I', 'S', 'T', '1'};
constexpr std::uint32_t kVersion = 1;
constexpr std::size_t kHeaderSize = 16;
constexpr std::size_t kNameBytes = 24;

std::uint32_t fnv1a(const unsigned char* data, std::size_t length) {
    std::uint32_t hash = 2166136261u;
    for (std::size_t i = 0; i < length; ++i) {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

template <typename T>
void putValue(unsigned char* buffer, std::size_t& offset, T value) {
    std::memcpy(buffer + offset, &value, sizeof(T));
    offset += sizeof(T);
}

template <typename T>
T getValue(const unsigned char* buffer, std::size_t& offset) {
    T value;
    std::memcpy(&value, buffer + offset, sizeof(T));
    offset += sizeof(T);
    return value;
}

std::string readWholeFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw FileAccessException(path, "cannot open for reading");
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

}  // namespace

const char* historyGameName(std::uint8_t game) {
    switch (game) {
        case 0: return "Blackjack";
        case 1: return "Red Dog";
        case 2: return "Poker";
        default: return "?";
    }
}

Storage::Storage(std::string directory) : directory_(std::move(directory)) {
    std::error_code ec;
    fs::create_directories(directory_, ec);
    if (ec) throw FileAccessException(directory_, ec.message());
}

// ------------------------------- profiles (text) -----------------------------

void Storage::saveProfiles(const std::map<std::string, PlayerProfile>& profiles) const {
    std::ostringstream body;
    body << "CASINO-PLAYERS 1\n" << profiles.size() << '\n';
    for (const auto& entry : profiles) body << entry.second << '\n';
    std::string text = body.str();
    text += "CHECKSUM " + sha256Hex(text) + "\n";

    // Write a temporary file first so a crash cannot leave a half-written profile file.
    const std::string tmp = profilesPath() + ".tmp";
    {
        // Binary mode: on Windows text mode would turn "\n" into "\r\n" and break the checksum.
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) throw FileAccessException(tmp, "cannot open for writing");
        out << text;
        out.flush();
        if (!out) throw FileAccessException(tmp, "write failed");
    }
    std::error_code ec;
    fs::remove(profilesPath(), ec);
    fs::rename(tmp, profilesPath(), ec);
    if (ec) throw FileAccessException(profilesPath(), ec.message());
}

std::map<std::string, PlayerProfile> Storage::loadProfiles() const {
    std::map<std::string, PlayerProfile> profiles;
    if (!fs::exists(profilesPath())) return profiles;

    const std::string text = readWholeFile(profilesPath());
    const std::size_t marker = text.rfind("CHECKSUM ");
    if (marker == std::string::npos) throw DataCorruptionException(profilesPath(), "checksum line missing");

    const std::string body = text.substr(0, marker);
    std::string stored = text.substr(marker + 9);
    while (!stored.empty() && (stored.back() == '\n' || stored.back() == '\r' || stored.back() == ' ')) stored.pop_back();
    if (sha256Hex(body) != stored) throw DataCorruptionException(profilesPath(), "checksum mismatch (file was edited)");

    std::istringstream in(body);
    std::string header;
    int version = 0;
    std::size_t count = 0;
    if (!(in >> header >> version >> count) || header != "CASINO-PLAYERS" || version != 1) {
        throw DataCorruptionException(profilesPath(), "unrecognised header");
    }
    for (std::size_t i = 0; i < count; ++i) {
        PlayerProfile profile;
        if (!(in >> profile)) throw DataCorruptionException(profilesPath(), "bad profile record #" + std::to_string(i + 1));
        // Keyed by lower-case name, the same key CasinoCore uses ("Alice" and "alice" are one player).
        std::string key = profile.name();
        for (char& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (!profiles.emplace(key, profile).second) {
            throw DataCorruptionException(profilesPath(), "duplicate player '" + profile.name() + "'");
        }
    }
    return profiles;
}

// ------------------------------- history (binary) ----------------------------

void Storage::appendHistory(const std::vector<HistoryRecord>& records) const {
    if (records.empty()) return;
    const std::string path = historyPath();
    const bool exists = fs::exists(path) && fs::file_size(path) >= kHeaderSize;

    if (exists) {  // refuse to append to a file that is not ours
        std::ifstream in(path, std::ios::binary);
        char magic[8];
        in.read(magic, 8);
        if (!in || std::memcmp(magic, kMagic, 8) != 0) throw DataCorruptionException(path, "bad file signature");
    }

    std::ofstream out(path, std::ios::binary | std::ios::app);
    if (!out) throw FileAccessException(path, "cannot open for appending");
    if (!exists) {
        unsigned char header[kHeaderSize] = {0};
        std::size_t off = 0;
        std::memcpy(header, kMagic, 8);
        off = 8;
        putValue<std::uint32_t>(header, off, kVersion);
        putValue<std::uint32_t>(header, off, 0);
        out.write(reinterpret_cast<const char*>(header), kHeaderSize);
    }

    for (const HistoryRecord& r : records) {
        unsigned char buf[kRecordSize] = {0};
        std::size_t off = 0;
        putValue<std::int64_t>(buf, off, r.timestamp);
        putValue<std::int32_t>(buf, off, r.round);
        putValue<std::uint8_t>(buf, off, r.game);
        char name[kNameBytes] = {0};
        std::strncpy(name, r.player.c_str(), kNameBytes - 1);
        std::memcpy(buf + off, name, kNameBytes);
        off += kNameBytes;
        putValue<std::int64_t>(buf, off, r.net);
        putValue<std::int64_t>(buf, off, r.balanceAfter);
        putValue<std::uint32_t>(buf, off, fnv1a(buf, off));  // checksum of everything before it
        out.write(reinterpret_cast<const char*>(buf), kRecordSize);
    }
    if (!out) throw FileAccessException(path, "write failed");
}

std::vector<HistoryRecord> Storage::loadHistory() const {
    std::vector<HistoryRecord> records;
    const std::string path = historyPath();
    if (!fs::exists(path)) return records;

    std::ifstream in(path, std::ios::binary);
    if (!in) throw FileAccessException(path, "cannot open for reading");

    unsigned char header[kHeaderSize];
    in.read(reinterpret_cast<char*>(header), kHeaderSize);
    if (in.gcount() != static_cast<std::streamsize>(kHeaderSize) || std::memcmp(header, kMagic, 8) != 0) {
        throw DataCorruptionException(path, "bad file signature");
    }
    std::size_t hoff = 8;
    if (getValue<std::uint32_t>(header, hoff) != kVersion) throw DataCorruptionException(path, "unsupported version");

    while (true) {
        unsigned char buf[kRecordSize];
        in.read(reinterpret_cast<char*>(buf), kRecordSize);
        const std::streamsize got = in.gcount();
        if (got == 0) break;
        if (got != static_cast<std::streamsize>(kRecordSize)) {
            throw DataCorruptionException(path, "truncated record #" + std::to_string(records.size() + 1));
        }
        std::size_t off = 0;
        HistoryRecord r;
        r.timestamp = getValue<std::int64_t>(buf, off);
        r.round = getValue<std::int32_t>(buf, off);
        r.game = getValue<std::uint8_t>(buf, off);
        char name[kNameBytes + 1] = {0};
        std::memcpy(name, buf + off, kNameBytes);
        off += kNameBytes;
        r.player = name;
        r.net = getValue<std::int64_t>(buf, off);
        r.balanceAfter = getValue<std::int64_t>(buf, off);
        const std::size_t checksumAt = off;
        const std::uint32_t stored = getValue<std::uint32_t>(buf, off);
        if (stored != fnv1a(buf, checksumAt)) {
            throw DataCorruptionException(path, "checksum mismatch in record #" + std::to_string(records.size() + 1));
        }
        records.push_back(r);
    }
    return records;
}

// ------------------------------- logs (text) ---------------------------------

void Storage::appendLedgerLog(const std::vector<Transaction>& transactions) const {
    if (transactions.empty()) return;
    std::ofstream out(ledgerPath(), std::ios::app);
    if (!out) throw FileAccessException(ledgerPath(), "cannot open for appending");
    for (const Transaction& tx : transactions) out << tx << '\n';
}

void Storage::appendFairnessLog(const std::vector<FairnessService::Record>& records) const {
    if (records.empty()) return;
    std::ofstream out(fairnessPath(), std::ios::app);
    if (!out) throw FileAccessException(fairnessPath(), "cannot open for appending");
    for (const auto& r : records) {
        out << r.id << ' ' << r.game << '#' << r.round << " decks=" << r.numDecks << " commitment=" << r.commitment
            << " seed=" << r.serverSeed << " order=" << r.orderHash << '\n';
    }
}

}  // namespace casino
