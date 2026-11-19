#include "Blackjack.hpp"

#include <algorithm>
#include <sstream>

#include "Exceptions.hpp"
#include "Json.hpp"
#include "Player.hpp"
#include "Templates.hpp"

namespace casino {

int blackjackCardValue(const Card& card) {
    if (card.isAce()) return 11;
    if (card.rank() >= 10) return 10;
    return card.rank();
}

BjValue blackjackValue(const std::vector<Card>& hand) {
    int total = 0;
    int aces = 0;
    for (const Card& c : hand) {
        total += blackjackCardValue(c);
        if (c.isAce()) ++aces;
    }
    while (total > 21 && aces > 0) {
        total -= 10;
        --aces;
    }
    return BjValue{total, aces > 0};
}

BjAction basicStrategy(const BlackjackView& v) {
    const int up = blackjackCardValue(v.dealerUp);  // 2..11 (ace = 11)
    const BjValue value = blackjackValue(v.cards);
    const auto doubleOrHit = [&] { return v.canDouble ? BjAction::Double : BjAction::Hit; };
    const auto doubleOrStand = [&] { return v.canDouble ? BjAction::Double : BjAction::Stand; };

    if (v.canSplit && v.cards.size() == 2) {
        switch (blackjackCardValue(v.cards[0])) {
            case 11:
            case 8: return BjAction::Split;
            case 9:
                if (up != 7 && up != 10 && up != 11) return BjAction::Split;
                break;
            case 7:
                if (up <= 7) return BjAction::Split;
                break;
            case 6:
                if (up <= 6) return BjAction::Split;
                break;
            case 4:
                if (up == 5 || up == 6) return BjAction::Split;
                break;
            case 3:
            case 2:
                if (up >= 4 && up <= 7) return BjAction::Split;
                break;
            default: break;  // never split 5s or tens
        }
    }

    if (value.soft) {
        if (value.total >= 19) return BjAction::Stand;
        if (value.total == 18) {
            if (up >= 3 && up <= 6) return doubleOrStand();
            if (up <= 8) return BjAction::Stand;
            return BjAction::Hit;
        }
        if (value.total == 17) return (up >= 3 && up <= 6) ? doubleOrHit() : BjAction::Hit;
        if (value.total >= 15) return (up >= 4 && up <= 6) ? doubleOrHit() : BjAction::Hit;
        return (up >= 5 && up <= 6) ? doubleOrHit() : BjAction::Hit;
    }

    const int t = value.total;
    if (t >= 17) return BjAction::Stand;
    if (t >= 13) return up <= 6 ? BjAction::Stand : BjAction::Hit;
    if (t == 12) return (up >= 4 && up <= 6) ? BjAction::Stand : BjAction::Hit;
    if (t == 11) return up <= 10 ? doubleOrHit() : BjAction::Hit;
    if (t == 10) return up <= 9 ? doubleOrHit() : BjAction::Hit;
    if (t == 9) return (up >= 3 && up <= 6) ? doubleOrHit() : BjAction::Hit;
    return BjAction::Hit;
}

// ================================ BlackjackGame ===============================

BlackjackGame::BlackjackGame(GameContext& ctx, std::string tableId) : Game(ctx, "Blackjack", std::move(tableId)) {}

std::string BlackjackGame::rules() const {
    return "BLACKJACK - beat the dealer without going over 21.\n"
           "  * 6-deck shoe, reshuffled when fewer than 100 cards remain.\n"
           "  * Cards 2-10 count face value, J/Q/K count 10, an ace counts 1 or 11.\n"
           "  * Blackjack (ace + ten-value card) pays 3 to 2. Other wins pay 1 to 1.\n"
           "  * Dealer stands on every 17. If the dealer has blackjack, play stops at once.\n"
           "  * Double down on any first two cards (one card only, stake doubles).\n"
           "  * Split a pair once (split aces get one card each). A split 21 pays 1 to 1.\n"
           "  * Bets: " + std::to_string(kMinBet) + "-" + std::to_string(kMaxBet) + " chips.";
}

void BlackjackGame::prepareShoe() {
    if (shoeReady_ && shoe_.remaining() >= kCutCard) return;
    if (shoeReady_) {
        const auto& old = ctx_.fairness.reveal(shoeRecordId_);
        ctx_.say("Previous shoe #" + std::to_string(old.id) + " seed revealed: " + old.serverSeed);
    }
    const auto& record = ctx_.fairness.commit(name_, ++shoeNumber_, kDecks);
    shoeRecordId_ = record.id;
    ctx_.fairness.shuffle(shoe_);
    shoeReady_ = true;
    ctx_.say("New " + std::to_string(kDecks) + "-deck shoe shuffled. Commitment: " + record.commitment);
}

void BlackjackGame::closeSession() {
    if (shoeReady_) ctx_.fairness.reveal(shoeRecordId_);
}

Card BlackjackGame::draw() { return shoe_.deal(); }

std::string BlackjackGame::describe(const std::vector<Card>& cards) {
    const BjValue v = blackjackValue(cards);
    std::string text = joinItems(cards) + " = " + (v.soft && v.total <= 21 ? "soft " : "") + std::to_string(v.total);
    if (v.total > 21) text += " (bust)";
    return text;
}

void BlackjackGame::publish() {
    if (liveSeats_ == nullptr || liveDealer_ == nullptr) return;
    std::vector<std::string> dealerCards;
    for (std::size_t i = 0; i < liveDealer_->size(); ++i) {
        dealerCards.push_back(json::quote(holeHidden_ && i == 1 ? std::string("??") : (*liveDealer_)[i].str()));
    }
    const std::vector<Card> visible = holeHidden_ ? std::vector<Card>{(*liveDealer_)[0]} : *liveDealer_;
    const BjValue dealerValue = blackjackValue(visible);

    std::vector<std::string> seatJson;
    for (const Seat& s : *liveSeats_) {
        std::vector<std::string> handJson;
        for (const Hand& h : s.hands) {
            const BjValue v = blackjackValue(h.cards);
            std::ostringstream o;
            o << "{\"cards\":" << json::cards(h.cards) << ",\"stake\":" << h.stake << ",\"total\":" << v.total
              << ",\"soft\":" << json::boolean(v.soft && v.total <= 21) << ",\"bust\":" << json::boolean(v.total > 21)
              << ",\"note\":" << json::quote(h.note) << "}";
            handJson.push_back(o.str());
        }
        std::ostringstream o;
        o << "{\"name\":" << json::quote(s.player->name()) << ",\"toAct\":"
          << json::boolean(!actor_.empty() && s.player->name() == actor_) << ",\"hands\":" << json::array(handJson) << "}";
        seatJson.push_back(o.str());
    }
    std::ostringstream t;
    t << "{\"game\":\"Blackjack\",\"dealer\":{\"cards\":" << json::array(dealerCards) << ",\"total\":" << dealerValue.total
      << "},\"seats\":" << json::array(seatJson) << "}";
    setTable(t.str());
}

void BlackjackGame::playSeat(Seat& seat, const Card& dealerUp) {
    Player& player = *seat.player;
    for (std::size_t hi = 0; hi < seat.hands.size(); ++hi) {
        while (true) {
            Hand& hand = seat.hands[hi];
            const BjValue value = blackjackValue(hand.cards);
            if (value.total >= 21) break;
            if (hand.splitAce && hand.cards.size() >= 2) break;  // split aces: one card only

            const long balance = ctx_.ledger.balance(player.account());
            BlackjackView view(dealerUp);
            view.cards = hand.cards;
            view.balance = balance;
            view.stake = hand.stake;
            view.handIndex = static_cast<int>(hi);
            view.handCount = static_cast<int>(seat.hands.size());
            view.total = value.total;
            view.soft = value.soft;
            view.canDouble = hand.cards.size() == 2 && balance >= hand.stake;
            view.canSplit = hand.cards.size() == 2 && seat.hands.size() == 1 &&
                            blackjackCardValue(hand.cards[0]) == blackjackCardValue(hand.cards[1]) &&
                            balance >= hand.stake;

            actor_ = player.name();
            publish();
            BjAction action = player.blackjackAction(view);
            actor_.clear();
            if (action == BjAction::Double && !view.canDouble) action = BjAction::Hit;  // illegal -> hit
            if (action == BjAction::Split && !view.canSplit) action = BjAction::Hit;

            if (action == BjAction::Stand) {
                ctx_.say("  " + player.name() + " stands on " + std::to_string(value.total) + ".");
                break;
            }
            if (action == BjAction::Hit) {
                hand.cards.push_back(draw());
                ctx_.say("  " + player.name() + " hits: " + describe(hand.cards));
                continue;
            }
            if (action == BjAction::Double) {
                escrow(player, hand.stake, "double down");
                hand.stake *= 2;
                hand.cards.push_back(draw());
                ctx_.say("  " + player.name() + " doubles: " + describe(hand.cards));
                break;
            }
            // Split: move one card to a new hand, pay a second stake, deal one card to each hand.
            escrow(player, hand.stake, "split");
            Hand second;
            second.cards.push_back(hand.cards.back());
            second.stake = hand.stake;
            second.fromSplit = true;
            second.splitAce = hand.cards.back().isAce();
            hand.cards.pop_back();
            hand.fromSplit = true;
            hand.splitAce = second.splitAce;
            hand.cards.push_back(draw());
            second.cards.push_back(draw());
            ctx_.say("  " + player.name() + " splits: " + describe(hand.cards) + "  |  " + describe(second.cards));
            seat.hands.push_back(second);  // may reallocate: `hand` is not used after this line
        }
    }
}

RoundResult BlackjackGame::playRound(std::vector<Player*>& seated) {
    RoundResult result;
    std::vector<Seat> seats;

    for (Player* p : seated) {
        const long balance = ctx_.ledger.balance(p->account());
        if (balance < kMinBet) {
            ctx_.say("  " + p->name() + " cannot cover the minimum bet and sits out.");
            continue;
        }
        try {
            BetView bv;
            bv.game = name_;
            bv.minBet = kMinBet;
            bv.maxBet = std::min(kMaxBet, balance);
            bv.balance = balance;
            const long bet = p->chooseBet(bv);
            if (bet == 0) {
                ctx_.say("  " + p->name() + " sits out this round.");
                continue;
            }
            checkedBet(bet, bv.minBet, bv.maxBet);
            escrow(*p, bet, "bet");
            Seat seat;
            seat.player = p;
            Hand hand;
            hand.stake = bet;
            seat.hands.push_back(hand);
            seats.push_back(seat);
        } catch (const InvalidBetException& e) {
            ctx_.say("  " + p->name() + ": " + e.what() + " - sitting out.");
        }
    }
    if (seats.empty()) {
        ctx_.say("No bets were placed.");
        return result;
    }

    result.played = true;
    countRound();
    prepareShoe();

    // Deal: one card each, dealer up-card, second card each, dealer hole card.
    for (Seat& s : seats) s.hands[0].cards.push_back(draw());
    std::vector<Card> dealer;
    dealer.push_back(draw());
    for (Seat& s : seats) s.hands[0].cards.push_back(draw());
    dealer.push_back(draw());

    liveSeats_ = &seats;
    liveDealer_ = &dealer;
    holeHidden_ = true;
    publish();
    ctx_.say("--- Blackjack round " + std::to_string(rounds_) + " ---");
    ctx_.say("Dealer shows " + joinItems(std::vector<Card>{dealer[0]}) + " [??]");
    for (const Seat& s : seats) {
        ctx_.say("  " + s.player->name() + ": " + describe(s.hands[0].cards) + "   stake " +
                 std::to_string(s.hands[0].stake));
    }

    const bool dealerBlackjack = blackjackValue(dealer).total == 21;
    if (dealerBlackjack) {
        ctx_.say("Dealer has blackjack! " + describe(dealer));
    } else {
        for (Seat& s : seats) playSeat(s, dealer[0]);
    }

    const auto isNatural = [](const Seat& seat, const Hand& hand) {
        return seat.hands.size() == 1 && !hand.fromSplit && hand.cards.size() == 2 &&
               blackjackValue(hand.cards).total == 21;
    };

    bool dealerMustPlay = false;
    for (const Seat& s : seats) {
        for (const Hand& h : s.hands) {
            if (blackjackValue(h.cards).total <= 21 && !isNatural(s, h)) dealerMustPlay = true;
        }
    }
    holeHidden_ = false;
    publish();
    if (!dealerBlackjack) ctx_.say("Dealer reveals " + describe(dealer));
    if (!dealerBlackjack && dealerMustPlay) {
        while (blackjackValue(dealer).total < 17) {
            dealer.push_back(draw());
            ctx_.say("  Dealer draws: " + describe(dealer));
            publish();
        }
    }
    const int dealerTotal = blackjackValue(dealer).total;

    // Settle every hand through the ledger.
    for (Seat& s : seats) {
        Player& p = *s.player;
        long net = 0;
        for (Hand& h : s.hands) {
            const BjValue v = blackjackValue(h.cards);
            result.wagered += h.stake;
            std::string verdict;
            if (v.total > 21) {
                settleLose(p, h.stake, "bust");
                net -= h.stake;
                verdict = "busts, loses " + std::to_string(h.stake);
            } else if (isNatural(s, h)) {
                if (dealerBlackjack) {
                    settlePush(p, h.stake, "blackjack push");
                    verdict = "blackjack pushes";
                } else {
                    const long profit = h.stake * 3 / 2;
                    settleWin(p, h.stake, profit, "blackjack");
                    net += profit;
                    verdict = "BLACKJACK! wins " + std::to_string(profit);
                }
            } else if (dealerBlackjack) {
                settleLose(p, h.stake, "dealer blackjack");
                net -= h.stake;
                verdict = "loses " + std::to_string(h.stake) + " to dealer blackjack";
            } else if (dealerTotal > 21 || v.total > dealerTotal) {
                settleWin(p, h.stake, h.stake, "win");
                net += h.stake;
                verdict = std::to_string(v.total) + " beats dealer, wins " + std::to_string(h.stake);
            } else if (v.total == dealerTotal) {
                settlePush(p, h.stake, "push");
                verdict = std::to_string(v.total) + " pushes";
            } else {
                settleLose(p, h.stake, "loss");
                net -= h.stake;
                verdict = std::to_string(v.total) + " loses to " + std::to_string(dealerTotal) + ", -" +
                          std::to_string(h.stake);
            }
            h.note = verdict;
            ctx_.say("  " + p.name() + ": " + verdict);
        }
        result.net[p.name()] = net;
    }
    sweepTable();
    publish();  // final state with every hand's result
    liveSeats_ = nullptr;  // the locals are about to go away
    liveDealer_ = nullptr;
    return result;
}

}  // namespace casino
