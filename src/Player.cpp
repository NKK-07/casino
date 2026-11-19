#include "Player.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>

#include "Blackjack.hpp"
#include "Console.hpp"
#include "PokerEval.hpp"
#include "Templates.hpp"

namespace casino {

int Player::created_ = 0;
int Player::alive_ = 0;

Player::Player(std::string name) : name_(std::move(name)) {
    ++created_;
    ++alive_;
}

Player::~Player() { --alive_; }

std::string Player::account() const {
    std::string id = "player:";
    for (char c : name_) id.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return id;
}

namespace {

const char* streetName(Street s) {
    switch (s) {
        case Street::PreFlop: return "pre-flop";
        case Street::Flop: return "flop";
        case Street::Turn: return "turn";
        case Street::River: return "river";
    }
    return "?";
}

}  // namespace

// ============================== HumanPlayer ==================================

long HumanPlayer::chooseBet(const BetView& view) {
    std::cout << '\n'
              << name() << " has " << console::money(view.balance) << " chips. " << view.game << " bet "
              << view.minBet << "-" << view.maxBet << " (0 to sit out).\n";
    while (true) {
        const long bet = console::readLong("  " + name() + "'s bet: ", 0, view.maxBet);
        if (bet == 0 || bet >= view.minBet) return bet;
        std::cout << "  ! The minimum bet is " << view.minBet << ".\n";
    }
}

BjAction HumanPlayer::blackjackAction(const BlackjackView& view) {
    std::cout << '\n'
              << name() << (view.handCount > 1 ? " (hand " + std::to_string(view.handIndex + 1) + ")" : "") << ": "
              << joinItems(view.cards) << " = " << (view.soft ? "soft " : "") << view.total << "   dealer shows "
              << view.dealerUp << "   stake " << view.stake << '\n';

    std::string allowed = "hs";
    std::string menu = "(h)it  (s)tand";
    if (view.canDouble) {
        allowed += 'd';
        menu += "  (d)ouble";
    }
    if (view.canSplit) {
        allowed += 'p';
        menu += "  s(p)lit";
    }
    switch (console::readChoice("  " + menu + ": ", allowed)) {
        case 'h': return BjAction::Hit;
        case 'd': return BjAction::Double;
        case 'p': return BjAction::Split;
        default: return BjAction::Stand;
    }
}

long HumanPlayer::redDogRaise(const RedDogView& view) {
    std::cout << '\n'
              << name() << ": spread " << view.spread << " (" << view.low << " and " << view.high << "), pays "
              << view.payout << " to 1.\n";
    if (view.maxRaise <= 0) {
        std::cout << "  (you have no chips left to raise with)\n";
        return 0;
    }
    return console::readLong("  Raise 0-" + std::to_string(view.maxRaise) + " (0 = no raise): ", 0, view.maxRaise);
}

PokerMove HumanPlayer::pokerAction(const PokerView& view) {
    if (hotSeat_) {
        console::clearScreen();
        std::cout << "*** Pass the keyboard to " << name() << ". Everyone else, look away! ***\n";
        console::pause("Press Enter when " + name() + " is ready...");
        console::rule();
        for (const std::string& line : view.recentActions) std::cout << "  " << line << '\n';
        console::rule();
    }

    std::cout << '\n' << name() << "'s turn (" << streetName(view.street) << ", stack " << view.stack << ")\n";
    std::cout << "  Your hand: " << joinItems(view.hole) << "   Board: "
              << (view.board.empty() ? std::string("(none yet)") : joinItems(view.board)) << '\n';
    std::cout << "  Pot: " << view.pot << "   To call: " << view.toCall << '\n';

    std::string allowed = "f";
    std::string menu = "(f)old  ";
    if (view.toCall == 0) {
        allowed += 'c';
        menu += "(c)heck";
    } else {
        allowed += 'c';
        menu += "(c)all " + std::to_string(view.toCall);
    }
    if (view.canRaise) {
        allowed += 'r';
        menu += "  (r)aise to " + std::to_string(view.minRaiseTo) + "-" + std::to_string(view.maxRaiseTo);
    }

    PokerMove move;
    switch (console::readChoice("  " + menu + ": ", allowed)) {
        case 'f':
            move.kind = PokerKind::Fold;
            break;
        case 'c':
            move.kind = view.toCall == 0 ? PokerKind::Check : PokerKind::Call;
            break;
        default:
            move.kind = PokerKind::Raise;
            move.raiseTo = console::readLong("  Raise to (" + std::to_string(view.minRaiseTo) + "-" +
                                                 std::to_string(view.maxRaiseTo) + "): ",
                                             view.minRaiseTo, view.maxRaiseTo);
            break;
    }
    if (hotSeat_) console::clearScreen();
    return move;
}

// =============================== BotPlayer ===================================

BotPlayer::BotPlayer(std::string name, Style style, std::uint64_t seed)
    : Player(std::move(name)), style_(style), rng_(seed) {}

long BotPlayer::chooseBet(const BetView& view) {
    if (view.balance < view.minBet) return 0;
    long multiplier = 1;
    if (style_ == Style::Tight) multiplier = 1 + static_cast<long>(rng_() % 2);
    if (style_ == Style::Aggressive) multiplier = 1 + static_cast<long>(rng_() % 3);
    return clampValue(view.minBet * multiplier, view.minBet, view.maxBet);
}

BjAction BotPlayer::blackjackAction(const BlackjackView& view) { return basicStrategy(view); }

long BotPlayer::redDogRaise(const RedDogView& view) {
    const int threshold = style_ == Style::Tight ? 8 : 7;  // raise only when the odds beat 50%
    return view.spread >= threshold ? view.maxRaise : 0;
}

PokerMove BotPlayer::pokerAction(const PokerView& view) {
    const double equity = estimateEquity(view.hole, view.board, std::max(1, view.opponentsInHand), trials_, rng_);
    const double raiseThreshold = style_ == Style::Aggressive ? 0.55 : (style_ == Style::Tight ? 0.72 : 0.64);
    const double bluffChance = style_ == Style::Aggressive ? 0.08 : 0.02;
    const double potOdds = view.toCall > 0 ? static_cast<double>(view.toCall) / (view.pot + view.toCall) : 0.0;
    const double roll = std::uniform_real_distribution<double>(0.0, 1.0)(rng_);

    // A raise sized as a fraction of the pot after calling, kept inside legal limits.
    auto raiseBy = [&](double fraction) {
        const long target = view.committedThisRound + view.toCall +
                            static_cast<long>(fraction * static_cast<double>(view.pot + view.toCall));
        PokerMove move;
        move.kind = PokerKind::Raise;
        move.raiseTo = clampValue(target, view.minRaiseTo, view.maxRaiseTo);
        return move;
    };

    PokerMove move;
    if (view.toCall == 0) {
        if (view.canRaise && (equity > raiseThreshold || roll < bluffChance)) return raiseBy(0.6);
        move.kind = PokerKind::Check;
        return move;
    }
    if (view.canRaise && equity > raiseThreshold + 0.10) return raiseBy(0.75);
    if (equity >= potOdds + 0.03) {
        move.kind = PokerKind::Call;
        return move;
    }
    if (view.canRaise && roll < bluffChance && view.toCall <= view.bigBlind * 3) return raiseBy(1.0);
    move.kind = PokerKind::Fold;
    return move;
}

}  // namespace casino
