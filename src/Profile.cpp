#include "Profile.hpp"

#include <algorithm>
#include <cctype>
#include <istream>
#include <ostream>

namespace casino {

bool isValidPlayerName(const std::string& name) {
    if (name.size() < 2 || name.size() > 16) return false;
    for (char c : name) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (!(std::isalnum(u) || c == '_' || c == '-')) return false;
    }
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower != "house" && lower != "rake" && lower != "table";
}

PlayerProfile& PlayerProfile::setBalance(long balance) {
    balance_ = balance;
    return *this;
}

PlayerProfile& PlayerProfile::recordResult(long net) {
    ++hands_;
    net_ += net;
    if (net > 0) {
        ++wins_;
        biggestWin_ = std::max(biggestWin_, net);
    }
    return *this;
}

std::ostream& operator<<(std::ostream& os, const PlayerProfile& p) {
    return os << p.name_ << ' ' << p.balance_ << ' ' << p.hands_ << ' ' << p.wins_ << ' ' << p.net_ << ' '
              << p.biggestWin_;
}

std::istream& operator>>(std::istream& is, PlayerProfile& p) {
    PlayerProfile loaded;
    if (!(is >> loaded.name_ >> loaded.balance_ >> loaded.hands_ >> loaded.wins_ >> loaded.net_ >>
          loaded.biggestWin_)) {
        return is;  // stream already in a failed state
    }
    if (!isValidPlayerName(loaded.name_) || loaded.balance_ < 0 || loaded.hands_ < 0 || loaded.wins_ < 0 ||
        loaded.wins_ > loaded.hands_ || loaded.biggestWin_ < 0) {
        is.setstate(std::ios::failbit);
        return is;
    }
    p = loaded;
    return is;
}

}  // namespace casino
