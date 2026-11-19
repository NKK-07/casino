#include "Poker.hpp"

#include <algorithm>
#include <queue>
#include <sstream>

#include "Exceptions.hpp"
#include "Json.hpp"
#include "Player.hpp"
#include "Templates.hpp"

namespace casino {

PokerGame::PokerGame(GameContext& ctx, std::string tableId) : Game(ctx, "Poker", std::move(tableId)) {}

std::string PokerGame::rules() const {
    return "TEXAS HOLD'EM (no limit) - 2 to 6 players, blinds " + std::to_string(kSmallBlind) + "/" +
           std::to_string(kBigBlind) + ".\n"
           "  * Each player gets two private cards; five community cards come in three steps:\n"
           "    flop (3), turn (1), river (1), with a betting round after each deal.\n"
           "  * Best five-card hand from your seven wins. Ties split the pot.\n"
           "  * Actions: fold, check/call, or raise. All-in players create side pots.\n"
           "  * You bring at most " + std::to_string(kMaxStack) + " chips to a hand.\n"
           "  * The dealer button moves every hand. Rake: " + std::to_string(kRakePercent) +
           "% of the pot (max " + std::to_string(kRakeCap) + "), only if a flop is dealt.";
}

std::string PokerGame::seatName(std::size_t seat) const { return seats_[seat].player->name(); }

void PokerGame::announce(const std::string& line) {
    recentActions_.push_back(line);
    if (recentActions_.size() > 10) recentActions_.pop_front();
    ctx_.say("  " + line);
    publish();
}

void PokerGame::publish() {
    static const char* const streets[4] = {"pre-flop", "flop", "turn", "river"};
    std::vector<std::string> seatJson;
    for (std::size_t i = 0; i < seats_.size(); ++i) {
        const Seat& s = seats_[i];
        std::ostringstream o;
        o << "{\"name\":" << json::quote(s.player->name()) << ",\"stack\":" << s.stack << ",\"bet\":" << s.roundBet
          << ",\"total\":" << s.totalBet << ",\"folded\":" << json::boolean(s.folded)
          << ",\"allIn\":" << json::boolean(s.allIn) << ",\"dealer\":" << json::boolean(i == button_)
          << ",\"toAct\":" << json::boolean(!actor_.empty() && s.player->name() == actor_)
          << ",\"note\":" << json::quote(s.note) << ",\"holeCount\":" << s.hole.size()
          << ",\"cards\":" << (s.shown ? json::cards(s.hole) : std::string("[]"))
          << ",\"hand\":" << json::quote(s.shown ? s.handText : std::string()) << "}";
        seatJson.push_back(o.str());
    }
    std::ostringstream t;
    t << "{\"game\":\"Poker\",\"street\":" << json::quote(streets[static_cast<int>(street_)]) << ",\"pot\":" << pot_
      << ",\"board\":" << json::cards(board_) << ",\"seats\":" << json::array(seatJson) << "}";
    setTable(t.str());
}

long PokerGame::put(Seat& seat, long amount) {
    amount = std::min(amount, seat.stack);
    if (amount <= 0) return 0;
    ctx_.ledger.transfer(seat.player->account(), tableAccount(), amount, name_ + ": bet (" + seat.player->name() + ")");
    seat.stack -= amount;
    seat.roundBet += amount;
    seat.totalBet += amount;
    pot_ += amount;
    if (seat.stack == 0) seat.allIn = true;
    return amount;
}

void PokerGame::postBlind(std::size_t seat, long amount, const char* label) {
    Seat& s = seats_[seat];
    const long paid = put(s, amount);
    s.note = std::string(label) + " " + std::to_string(paid);
    announce(s.player->name() + " posts the " + label + " (" + std::to_string(paid) + ")" +
             (s.allIn ? " and is all-in" : ""));
}

std::size_t PokerGame::liveCount() const {
    std::size_t live = 0;
    for (const Seat& s : seats_) {
        if (!s.folded) ++live;
    }
    return live;
}

void PokerGame::dealBoard(int count) {
    for (int i = 0; i < count; ++i) board_.push_back(deck_.deal());
}

PokerView PokerGame::makeView(std::size_t idx) const {
    const Seat& s = seats_[idx];
    PokerView v;
    v.hole = s.hole;
    v.board = board_;
    v.street = street_;
    v.pot = pot_;
    const long owed = std::max(currentBet_ - s.roundBet, 0L);
    v.toCall = std::min(owed, s.stack);
    v.stack = s.stack;
    v.committedThisRound = s.roundBet;
    v.maxRaiseTo = s.roundBet + s.stack;
    v.canRaise = v.maxRaiseTo > currentBet_;
    v.minRaiseTo = std::min(currentBet_ + lastRaise_, v.maxRaiseTo);
    v.bigBlind = kBigBlind;
    v.opponentsInHand = static_cast<int>(liveCount()) - 1;
    v.recentActions.assign(recentActions_.begin(), recentActions_.end());
    return v;
}

void PokerGame::bettingRound(Street street, std::size_t firstToAct) {
    street_ = street;
    const std::size_t n = seats_.size();
    if (street == Street::PreFlop) {
        currentBet_ = 0;
        for (const Seat& s : seats_) currentBet_ = std::max(currentBet_, s.roundBet);  // blinds already posted
    } else {
        for (Seat& s : seats_) {
            s.roundBet = 0;
            if (!s.folded) s.note.clear();  // a new betting round starts fresh
        }
        currentBet_ = 0;
    }
    lastRaise_ = kBigBlind;

    const auto canAct = [](const Seat& s) { return !s.folded && !s.allIn; };

    // STL queue holds the seats that still have to act, in turn order.
    std::queue<std::size_t> toAct;
    for (std::size_t k = 0; k < n; ++k) {
        const std::size_t idx = (firstToAct + k) % n;
        if (canAct(seats_[idx])) toAct.push(idx);
    }

    while (!toAct.empty() && liveCount() > 1) {
        const std::size_t idx = toAct.front();
        toAct.pop();
        Seat& s = seats_[idx];
        if (!canAct(s)) continue;

        std::size_t others = 0;
        for (std::size_t j = 0; j < n; ++j) {
            if (j != idx && canAct(seats_[j])) ++others;
        }
        const long owed = std::max(currentBet_ - s.roundBet, 0L);
        if (others == 0 && owed == 0) continue;  // nobody left to bet against

        const PokerView view = makeView(idx);
        actor_ = s.player->name();
        publish();  // everyone can see whose turn it is
        PokerMove move = s.player->pokerAction(view);
        actor_.clear();

        // Sanitise: bad requests degrade to the nearest legal action.
        if (move.kind == PokerKind::Check && owed > 0) move.kind = PokerKind::Call;
        if (move.kind == PokerKind::Raise && !view.canRaise) move.kind = owed > 0 ? PokerKind::Call : PokerKind::Check;

        const std::string name = s.player->name();
        switch (move.kind) {
            case PokerKind::Fold:
                s.folded = true;
                s.note = "Folded";
                announce(name + " folds.");
                break;
            case PokerKind::Check:
                s.note = "Checked";
                announce(name + " checks.");
                break;
            case PokerKind::Call: {
                const long paid = put(s, owed);
                s.note = s.allIn ? "All-in" : "Called " + std::to_string(paid);
                announce(name + " calls " + std::to_string(paid) + (s.allIn ? " (all-in)." : "."));
                break;
            }
            case PokerKind::Raise: {
                const long raiseTo = clampValue(move.raiseTo, view.minRaiseTo, view.maxRaiseTo);
                const bool opening = currentBet_ == 0;
                put(s, raiseTo - s.roundBet);
                const long increase = raiseTo - currentBet_;
                if (increase >= lastRaise_) lastRaise_ = increase;
                currentBet_ = raiseTo;
                s.note = s.allIn ? "All-in" : (opening ? "Bet " : "Raised to ") + std::to_string(raiseTo);
                announce(name + (opening ? " bets " : " raises to ") + std::to_string(raiseTo) +
                         (s.allIn ? " (all-in)." : "."));
                toAct = std::queue<std::size_t>();  // everyone else must respond again
                for (std::size_t k = 1; k < n; ++k) {
                    const std::size_t j = (idx + k) % n;
                    if (canAct(seats_[j])) toAct.push(j);
                }
                break;
            }
        }
    }
}

void PokerGame::settleHand() {
    const std::size_t n = seats_.size();
    std::vector<std::size_t> live;
    for (std::size_t i = 0; i < n; ++i) {
        if (!seats_[i].folded) live.push_back(i);
    }

    const long rake = board_.size() >= 3 ? std::min(kRakeCap, pot_ * kRakePercent / 100) : 0;
    std::vector<long> payout(n, 0);

    if (live.size() == 1) {
        payout[live[0]] = pot_ - rake;
        ctx_.say("  " + seatName(live[0]) + " wins " + std::to_string(payout[live[0]]) +
                 " - everyone else folded.");
    } else {
        ctx_.say("--- Showdown ---");
        std::vector<HandRank> ranks(n);
        for (std::size_t i : live) {
            std::vector<Card> cards = seats_[i].hole;
            cards.insert(cards.end(), board_.begin(), board_.end());
            ranks[i] = evaluateBest(cards);
            seats_[i].shown = true;  // cards go face up for everyone
            seats_[i].handText = ranks[i].describe();
            ctx_.say("  " + seatName(i) + ": " + joinItems(seats_[i].hole) + " -> " + ranks[i].describe());
        }

        std::vector<Contribution> contributions;
        for (std::size_t i = 0; i < n; ++i) {
            contributions.push_back(Contribution{i, seats_[i].totalBet, seats_[i].folded});
        }
        std::vector<SidePot> pots = buildSidePots(contributions);

        long rakeLeft = rake;  // the house takes its rake from the first pot(s)
        for (SidePot& pot : pots) {
            const long take = std::min(rakeLeft, pot.amount);
            pot.amount -= take;
            rakeLeft -= take;
            if (rakeLeft == 0) break;
        }

        for (std::size_t p = 0; p < pots.size(); ++p) {
            const SidePot& pot = pots[p];
            if (pot.amount == 0) continue;
            HandRank best;
            for (std::size_t seat : pot.eligible) best = std::max(best, ranks[seat]);
            std::vector<std::size_t> winners;
            for (std::size_t seat : pot.eligible) {
                if (ranks[seat] == best) winners.push_back(seat);
            }
            // Odd chips go to the winners closest to the left of the button.
            std::sort(winners.begin(), winners.end(), [&](std::size_t a, std::size_t b) {
                return (a + n - button_ - 1) % n < (b + n - button_ - 1) % n;
            });
            const long share = pot.amount / static_cast<long>(winners.size());
            long remainder = pot.amount % static_cast<long>(winners.size());
            std::vector<std::string> names;
            for (std::size_t w : winners) {
                payout[w] += share + (remainder > 0 ? 1 : 0);
                if (remainder > 0) --remainder;
                names.push_back(seatName(w));
            }
            ctx_.say(std::string("  ") + (p == 0 ? "Main pot " : "Side pot " + std::to_string(p) + " ") +
                     std::to_string(pot.amount) + ": " + joinItems(names, ", ") +
                     (winners.size() > 1 ? " split." : " wins."));
        }
    }

    long paidOut = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (payout[i] > 0) seats_[i].note = "Wins " + std::to_string(payout[i]);
    }
    actor_.clear();
    for (std::size_t i = 0; i < n; ++i) {
        if (payout[i] > 0) {
            ctx_.ledger.transfer(tableAccount(), seats_[i].player->account(), payout[i],
                                 name_ + ": pot won (" + seatName(i) + ")");
            paidOut += payout[i];
        }
    }
    if (paidOut > pot_) throw CasinoException("Poker payout exceeds the pot - internal error");
    if (pot_ - paidOut > 0) {
        ctx_.ledger.transfer(tableAccount(), Ledger::RAKE, pot_ - paidOut, name_ + ": rake");
        ctx_.say("  House rake: " + std::to_string(pot_ - paidOut));
    }
    publish();
}

RoundResult PokerGame::playRound(std::vector<Player*>& seated) {
    RoundResult result;
    seats_.clear();
    board_.clear();
    recentActions_.clear();
    pot_ = 0;

    for (Player* p : seated) {
        const long balance = ctx_.ledger.balance(p->account());
        if (balance < kBigBlind) {
            ctx_.say("  " + p->name() + " cannot cover the big blind and sits out.");
            continue;
        }
        Seat seat;
        seat.player = p;
        seat.stack = std::min(balance, kMaxStack);
        seat.startBalance = balance;
        seats_.push_back(seat);
    }
    if (seats_.size() < 2) {
        ctx_.say("Poker needs at least two players with chips.");
        return result;
    }

    result.played = true;
    countRound();
    const std::size_t n = seats_.size();
    button_ = static_cast<std::size_t>(buttonCounter_++) % n;

    const auto& record = ctx_.fairness.commit(name_, rounds_, 1);
    const long recordId = record.id;
    ctx_.fairness.shuffle(deck_);

    ctx_.say("--- Poker hand " + std::to_string(rounds_) + " ---");
    ctx_.say("Dealer button: " + seatName(button_) + ". Shuffle commitment: " + record.commitment);

    // Heads-up: the button posts the small blind and acts first before the flop.
    const std::size_t sb = n == 2 ? button_ : (button_ + 1) % n;
    const std::size_t bb = n == 2 ? (button_ + 1) % n : (button_ + 2) % n;
    postBlind(sb, kSmallBlind, "small blind");
    postBlind(bb, kBigBlind, "big blind");

    for (int round = 0; round < 2; ++round) {
        for (std::size_t k = 1; k <= n; ++k) seats_[(button_ + k) % n].hole.push_back(deck_.deal());
    }
    ctx_.say("Hole cards dealt.");
    publish();

    bettingRound(Street::PreFlop, n == 2 ? sb : (bb + 1) % n);

    static const Street postflop[3] = {Street::Flop, Street::Turn, Street::River};
    static const char* const streetLabel[3] = {"Flop", "Turn", "River"};
    for (int k = 0; k < 3 && liveCount() > 1; ++k) {
        dealBoard(k == 0 ? 3 : 1);
        street_ = postflop[k];
        ctx_.say(std::string(streetLabel[k]) + ": " + joinItems(board_));
        publish();
        bettingRound(postflop[k], (button_ + 1) % n);
    }

    settleHand();

    const auto& revealed = ctx_.fairness.reveal(recordId);
    ctx_.say("Seed revealed: " + revealed.serverSeed + " (verify: " +
             std::string(FairnessService::verify(revealed) ? "OK" : "FAILED") + ")");
    sweepTable();

    for (const Seat& s : seats_) {
        const long after = ctx_.ledger.balance(s.player->account());
        result.net[s.player->name()] = after - s.startBalance;
        result.wagered += s.totalBet;
    }
    return result;
}

}  // namespace casino
