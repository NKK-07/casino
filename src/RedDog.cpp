#include "RedDog.hpp"

#include <algorithm>
#include <sstream>

#include "Deck.hpp"
#include "Exceptions.hpp"
#include "Json.hpp"
#include "Player.hpp"
#include "Templates.hpp"

namespace casino {

int redDogPayout(int spread) {
    if (spread == 1) return 5;
    if (spread == 2) return 4;
    if (spread == 3) return 2;
    return 1;
}

RedDogGame::RedDogGame(GameContext& ctx, std::string tableId) : Game(ctx, "Red Dog", std::move(tableId)) {}

std::string RedDogGame::rules() const {
    return "RED DOG - will the third card land between the first two?\n"
           "  * Everyone antes. The dealer turns up two cards from a fresh deck.\n"
           "  * Spread = number of ranks strictly between them (ace is high).\n"
           "  * Pair: push, unless the third card is the same rank - that pays 11 to 1.\n"
           "  * Consecutive ranks (spread 0): push.\n"
           "  * Otherwise you may raise up to your ante, then the third card is dealt.\n"
           "    Between the two cards you win, otherwise you lose ante + raise.\n"
           "  * Pays 5-1 on spread 1, 4-1 on spread 2, 2-1 on spread 3, 1-1 on spread 4+.\n"
           "  * Bets: " + std::to_string(kMinBet) + "-" + std::to_string(kMaxBet) + " chips.";
}

void RedDogGame::publish() {
    if (liveSeats_ == nullptr) return;
    std::vector<std::string> seatJson;
    for (const Seat& s : *liveSeats_) {
        std::ostringstream o;
        o << "{\"name\":" << json::quote(s.player->name()) << ",\"stake\":" << s.stake << ",\"raise\":" << s.raise
          << ",\"toAct\":" << json::boolean(!actor_.empty() && s.player->name() == actor_)
          << ",\"note\":" << json::quote(s.note) << "}";
        seatJson.push_back(o.str());
    }
    std::ostringstream t;
    t << "{\"game\":\"Red Dog\",\"cards\":" << json::strings(cards_) << ",\"spread\":" << spread_
      << ",\"payout\":" << payout_ << ",\"seats\":" << json::array(seatJson) << "}";
    setTable(t.str());
}

RoundResult RedDogGame::playRound(std::vector<Player*>& seated) {
    RoundResult result;
    std::vector<Seat> seats;

    for (Player* p : seated) {
        const long balance = ctx_.ledger.balance(p->account());
        if (balance < kMinBet) {
            ctx_.say("  " + p->name() + " cannot cover the minimum ante and sits out.");
            continue;
        }
        try {
            BetView bv;
            bv.game = name_;
            bv.minBet = kMinBet;
            bv.maxBet = std::min(kMaxBet, balance);
            bv.balance = balance;
            const long ante = p->chooseBet(bv);
            if (ante == 0) {
                ctx_.say("  " + p->name() + " sits out this round.");
                continue;
            }
            checkedBet(ante, bv.minBet, bv.maxBet);
            escrow(*p, ante, "ante");
            seats.push_back(Seat{p, ante, 0, std::string()});
        } catch (const InvalidBetException& e) {
            ctx_.say("  " + p->name() + ": " + e.what() + " - sitting out.");
        }
    }
    if (seats.empty()) {
        ctx_.say("No antes were placed.");
        return result;
    }

    result.played = true;
    countRound();
    liveSeats_ = &seats;
    cards_.clear();
    spread_ = -1;
    payout_ = 0;
    actor_.clear();
    publish();

    // Fresh deck, committed and shuffled for this round only.
    const auto& record = ctx_.fairness.commit(name_, rounds_, 1);
    const long recordId = record.id;
    Deck deck(1);
    ctx_.fairness.shuffle(deck);
    ctx_.say("--- Red Dog round " + std::to_string(rounds_) + " ---");
    ctx_.say("Shuffle commitment: " + record.commitment);

    Card first = deck.deal();
    Card second = deck.deal();
    if (second < first) std::swap(first, second);
    // Ordering by rank only: compare ranks explicitly in case suits swapped them.
    if (second.rank() < first.rank()) std::swap(first, second);
    const int spread = second.rank() - first.rank() - 1;
    cards_ = {first.str(), second.str(), "??"};
    spread_ = first.rank() == second.rank() ? 0 : spread;
    publish();
    ctx_.say("Dealer turns up " + joinItems(std::vector<Card>{first, second}));

    const auto finish = [&](Seat& s, const std::string& verdict, long net, int outcome) {
        // outcome: 1 win, 0 push, -1 lose; `net` is the profit/loss in chips
        if (outcome > 0) settleWin(*s.player, s.stake, net, "win");
        else if (outcome == 0) settlePush(*s.player, s.stake, "push");
        else settleLose(*s.player, s.stake, "loss");
        result.net[s.player->name()] = outcome < 0 ? -s.stake : (outcome == 0 ? 0 : net);
        s.note = verdict;
        ctx_.say("  " + s.player->name() + ": " + verdict);
    };

    if (first.rank() == second.rank()) {
        const Card third = deck.deal();
        cards_[2] = third.str();
        publish();
        ctx_.say("It's a pair! Third card: " + joinItems(std::vector<Card>{third}));
        for (Seat& s : seats) {
            result.wagered += s.stake;
            if (third.rank() == first.rank()) {
                finish(s, "three of a kind pays " + std::to_string(kPairPayout) + " to 1!", s.stake * kPairPayout, 1);
            } else {
                finish(s, "push", 0, 0);
            }
        }
    } else if (spread == 0) {
        ctx_.say("Consecutive cards - no spread. Everyone pushes.");
        for (Seat& s : seats) {
            result.wagered += s.stake;
            finish(s, "push", 0, 0);
        }
    } else {
        const int payout = redDogPayout(spread);
        payout_ = payout;
        publish();
        ctx_.say("Spread " + std::to_string(spread) + ", pays " + std::to_string(payout) + " to 1.");

        // Raise decisions, one player at a time.
        for (Seat& s : seats) {
            RedDogView view;
            view.low = first;
            view.high = second;
            view.spread = spread;
            view.ante = s.stake;
            view.maxRaise = std::min(s.stake, ctx_.ledger.balance(s.player->account()));
            view.payout = payout;
            actor_ = s.player->name();
            publish();
            long raise = s.player->redDogRaise(view);
            actor_.clear();
            raise = clampValue<long>(raise, 0, view.maxRaise);
            if (raise > 0) {
                escrow(*s.player, raise, "raise");
                s.stake += raise;
                s.raise = raise;
                publish();
                ctx_.say("  " + s.player->name() + " raises " + std::to_string(raise) + " (total at risk " +
                         std::to_string(s.stake) + ").");
            }
        }

        const Card third = deck.deal();
        cards_[2] = third.str();
        publish();
        const bool between = third.rank() > first.rank() && third.rank() < second.rank();
        ctx_.say("Third card: " + joinItems(std::vector<Card>{third}) + (between ? " - IN BETWEEN!" : " - outside."));
        for (Seat& s : seats) {
            result.wagered += s.stake;
            if (between) {
                finish(s, "wins " + std::to_string(s.stake * payout), s.stake * payout, 1);
            } else {
                finish(s, "loses " + std::to_string(s.stake), 0, -1);
            }
        }
    }

    const auto& revealed = ctx_.fairness.reveal(recordId);
    ctx_.say("Seed revealed: " + revealed.serverSeed + " (verify: " +
             std::string(FairnessService::verify(revealed) ? "OK" : "FAILED") + ")");
    sweepTable();
    publish();  // final state with each player's result
    liveSeats_ = nullptr;  // the local seats are about to go away
    return result;
}

}  // namespace casino
