// Profile.hpp - a player's saved record (Unit III: overloading << and >>).
#pragma once

#include <iosfwd>
#include <string>

namespace casino {

// Names are 2-16 characters from [A-Za-z0-9_-] and may not clash with ledger accounts.
bool isValidPlayerName(const std::string& name);

class PlayerProfile {
public:
    PlayerProfile() = default;
    PlayerProfile(std::string name, long balance) : name_(std::move(name)), balance_(balance) {}

    const std::string& name() const { return name_; }
    long balance() const { return balance_; }
    long hands() const { return hands_; }
    long wins() const { return wins_; }
    long lifetimeNet() const { return net_; }
    long biggestWin() const { return biggestWin_; }

    // Setters return *this (via the `this` pointer) so calls can be chained.
    PlayerProfile& setBalance(long balance);
    PlayerProfile& recordResult(long net);

    // Profiles compare by lifetime winnings.
    bool operator<(const PlayerProfile& other) const { return net_ < other.net_; }

    // Text-file form: "name balance hands wins net biggestWin".
    friend std::ostream& operator<<(std::ostream& os, const PlayerProfile& profile);
    friend std::istream& operator>>(std::istream& is, PlayerProfile& profile);

private:
    std::string name_;
    long balance_ = 0;
    long hands_ = 0;
    long wins_ = 0;
    long net_ = 0;
    long biggestWin_ = 0;
};

}  // namespace casino
