// tests.cpp - automated tests for the casino engine. No external framework:
// a TEST is a function registered in a list, CHECK records failures.
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "Blackjack.hpp"
#include "Card.hpp"
#include "CasinoCore.hpp"
#include "Console.hpp"
#include "Deck.hpp"
#include "Exceptions.hpp"
#include "Fairness.hpp"
#include "Game.hpp"
#include "Ledger.hpp"
#include "Player.hpp"
#include "Poker.hpp"
#include "PokerEval.hpp"
#include "Profile.hpp"
#include "RedDog.hpp"
#include "Sha256.hpp"
#include "Simulator.hpp"
#include "Storage.hpp"
#include "Templates.hpp"

using namespace casino;

// ------------------------------------------------------------------ harness
namespace {

struct TestCase {
    std::string name;
    std::function<void()> body;
};

std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

int g_failures = 0;
std::string g_current;

struct Registrar {
    Registrar(const std::string& name, std::function<void()> body) { registry().push_back({name, std::move(body)}); }
};

void fail(const std::string& what, int line) {
    ++g_failures;
    std::cout << "    FAIL [" << g_current << "] line " << line << ": " << what << '\n';
}

}  // namespace

#define TEST(name)                                   \
    static void name();                              \
    static Registrar registrar_##name(#name, name);  \
    static void name()

#define CHECK(cond) \
    do {            \
        if (!(cond)) fail(#cond, __LINE__); \
    } while (0)

#define CHECK_EQ(a, b) \
    do {               \
        if (!((a) == (b))) { std::ostringstream os_; os_ << #a " == " #b " (got " << (a) << " vs " << (b) << ")"; fail(os_.str(), __LINE__); } \
    } while (0)

#define CHECK_THROWS(ExceptionType, expr)                                   \
    do {                                                                    \
        bool caught_ = false;                                               \
        try { expr; } catch (const ExceptionType&) { caught_ = true; }      \
        catch (...) { fail("wrong exception type from " #expr, __LINE__); caught_ = true; } \
        if (!caught_) fail("no exception from " #expr, __LINE__);           \
    } while (0)

// ------------------------------------------------------------------ helpers
namespace {

// "AS", "10H", "7d" -> Card
Card card(const std::string& code) {
    const std::string rank = code.substr(0, code.size() - 1);
    const char suit = static_cast<char>(std::toupper(static_cast<unsigned char>(code.back())));
    int r = 0;
    if (rank == "A") r = 14;
    else if (rank == "K") r = 13;
    else if (rank == "Q") r = 12;
    else if (rank == "J") r = 11;
    else r = std::stoi(rank);
    const Suit s = suit == 'S' ? Suit::Spades : suit == 'H' ? Suit::Hearts : suit == 'D' ? Suit::Diamonds : Suit::Clubs;
    return Card(r, s);
}

std::vector<Card> cards(const std::vector<std::string>& codes) {
    std::vector<Card> out;
    for (const auto& c : codes) out.push_back(card(c));
    return out;
}

HandRank rank(const std::vector<std::string>& codes) { return evaluateBest(cards(codes)); }

// A player whose poker answer is fixed - lets a test script a hand exactly.
// (Another subclass of the abstract Player: the games cannot tell it apart.)
class ScriptedPlayer : public Player {
public:
    ScriptedPlayer(std::string name, PokerKind move, long bet = 10)
        : Player(std::move(name)), move_(move), bet_(bet) {}
    bool isHuman() const override { return false; }
    long chooseBet(const BetView& v) override { return clampValue(bet_, v.minBet, v.maxBet); }
    BjAction blackjackAction(const BlackjackView&) override { return BjAction::Stand; }
    long redDogRaise(const RedDogView&) override { return 0; }
    PokerMove pokerAction(const PokerView& v) override {
        PokerMove m;
        m.kind = move_;
        if (move_ == PokerKind::Check && v.toCall > 0) m.kind = PokerKind::Call;
        if (move_ == PokerKind::Raise) m.raiseTo = v.maxRaiseTo;  // shove
        return m;
    }

private:
    PokerKind move_;
    long bet_;
};

const std::string kTestDir = "build/test-data";

}  // namespace

// ================================================================== tests

TEST(sha256_known_vectors) {
    CHECK_EQ(sha256Hex("abc"), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    CHECK_EQ(sha256Hex(""), std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    CHECK_EQ(sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
             std::string("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
}

TEST(card_basics_and_operators) {
    CHECK_EQ(card("10H").str(), std::string("10H"));
    CHECK_EQ(card("AS").rankName(), std::string("A"));
    CHECK(card("2C") < card("3C"));
    CHECK(card("AS") == card("AS"));
    CHECK(card("AS") != card("AH"));
    std::ostringstream os;
    os << card("QD");
    CHECK_EQ(os.str(), std::string("[QD]"));
    CHECK_THROWS(InvalidInputException, Card(15, Suit::Spades));
}

TEST(deck_has_unique_cards_and_runs_out) {
    Deck one(1);
    std::set<std::string> seen;
    for (int i = 0; i < 52; ++i) seen.insert(one.deal().str());
    CHECK_EQ(seen.size(), static_cast<std::size_t>(52));
    CHECK_EQ(one.remaining(), static_cast<std::size_t>(0));
    CHECK_THROWS(EmptyDeckException, one.deal());
    Deck six(6);
    CHECK_EQ(six.size(), static_cast<std::size_t>(312));
}

TEST(shuffle_is_deterministic_per_seed) {
    Deck a(1), b(1), c(1);
    a.shuffle(42);
    b.shuffle(42);
    c.shuffle(43);
    CHECK_EQ(a.orderFingerprint(), b.orderFingerprint());
    CHECK(a.orderFingerprint() != c.orderFingerprint());
    Deck fresh(1);
    CHECK(a.orderFingerprint() != fresh.orderFingerprint());
}

TEST(fairness_commit_reveal_verifies) {
    FairnessService fairness;
    const auto& record = fairness.commit("Poker", 1, 1);
    const long id = record.id;
    CHECK_EQ(record.commitment, sha256Hex(record.serverSeed));
    Deck deck(1);
    fairness.shuffle(deck);
    const auto& revealed = fairness.reveal(id);
    CHECK(revealed.revealed);
    CHECK(FairnessService::verify(revealed));
    FairnessService::Record tampered = revealed;
    tampered.serverSeed[0] = tampered.serverSeed[0] == 'a' ? 'b' : 'a';
    CHECK(!FairnessService::verify(tampered));
}

TEST(poker_hand_categories_rank_in_order) {
    const HandRank royal = rank({"AS", "KS", "QS", "JS", "10S"});
    const HandRank straightFlush = rank({"9H", "8H", "7H", "6H", "5H"});
    const HandRank quads = rank({"7C", "7D", "7H", "7S", "2C"});
    const HandRank fullHouse = rank({"KC", "KD", "KH", "2S", "2C"});
    const HandRank flush = rank({"AD", "9D", "7D", "4D", "2D"});
    const HandRank straight = rank({"9C", "8D", "7H", "6S", "5C"});
    const HandRank trips = rank({"QC", "QD", "QH", "9S", "2C"});
    const HandRank twoPair = rank({"JC", "JD", "4H", "4S", "AC"});
    const HandRank pair = rank({"10C", "10D", "8H", "4S", "2C"});
    const HandRank high = rank({"AC", "JD", "8H", "4S", "2C"});
    CHECK(royal > straightFlush);
    CHECK(straightFlush > quads);
    CHECK(quads > fullHouse);
    CHECK(fullHouse > flush);
    CHECK(flush > straight);
    CHECK(straight > trips);
    CHECK(trips > twoPair);
    CHECK(twoPair > pair);
    CHECK(pair > high);
    CHECK_EQ(royal.describe(), std::string("Royal flush"));
    CHECK(fullHouse.category() == HandCategory::FullHouse);
}

TEST(poker_wheel_kickers_and_ties) {
    const HandRank wheel = rank({"AC", "2D", "3H", "4S", "5C"});
    const HandRank sixHigh = rank({"2C", "3D", "4H", "5S", "6C"});
    CHECK(wheel.category() == HandCategory::Straight);
    CHECK(sixHigh > wheel);
    CHECK(rank({"AC", "AD", "KH", "7S", "2C"}) > rank({"AH", "AS", "QH", "7C", "2D"}));  // kicker decides
    CHECK(rank({"AC", "AD", "KH", "7S", "2C"}) == rank({"AH", "AS", "KD", "7C", "2D"}));  // suits never matter
    // best five of seven: the flush hides among seven cards
    CHECK(rank({"2H", "9H", "KC", "5H", "QH", "3S", "7H"}).category() == HandCategory::Flush);
    CHECK_THROWS(InvalidInputException, evaluateBest(cards({"AS", "KS"})));
}

TEST(side_pots_split_correctly) {
    // A all-in for 50, B and C put in 100, D folded after putting in 30.
    const std::vector<Contribution> in = {{0, 50, false}, {1, 100, false}, {2, 100, false}, {3, 30, true}};
    const auto pots = buildSidePots(in);
    CHECK_EQ(pots.size(), static_cast<std::size_t>(2));
    CHECK_EQ(pots[0].amount, 180L);  // 50 + 50 + 50 + 30
    CHECK_EQ(pots[0].eligible.size(), static_cast<std::size_t>(3));
    CHECK_EQ(pots[1].amount, 100L);  // the extra 50 from B and C
    CHECK_EQ(pots[1].eligible.size(), static_cast<std::size_t>(2));
    long total = 0;
    for (const auto& p : pots) total += p.amount;
    CHECK_EQ(total, 280L);
}

TEST(blackjack_totals_and_soft_hands) {
    BjValue v = blackjackValue(cards({"AS", "6D"}));
    CHECK_EQ(v.total, 17);
    CHECK(v.soft);
    v = blackjackValue(cards({"AS", "6D", "10C"}));
    CHECK_EQ(v.total, 17);
    CHECK(!v.soft);
    v = blackjackValue(cards({"AS", "AD", "9C"}));
    CHECK_EQ(v.total, 21);
    v = blackjackValue(cards({"KS", "QD", "5C"}));
    CHECK_EQ(v.total, 25);
}

TEST(basic_strategy_used_by_bots) {
    auto decide = [](const std::vector<std::string>& hand, const std::string& up, bool canDouble, bool canSplit) {
        BlackjackView v(card(up));
        v.cards = cards(hand);
        const BjValue value = blackjackValue(v.cards);
        v.total = value.total;
        v.soft = value.soft;
        v.canDouble = canDouble;
        v.canSplit = canSplit;
        return basicStrategy(v);
    };
    CHECK(decide({"10S", "6D"}, "10C", true, false) == BjAction::Hit);    // hard 16 vs 10
    CHECK(decide({"10S", "2D"}, "4C", true, false) == BjAction::Stand);   // hard 12 vs 4
    CHECK(decide({"6S", "5D"}, "6C", true, false) == BjAction::Double);   // 11 vs 6
    CHECK(decide({"6S", "5D"}, "6C", false, false) == BjAction::Hit);     // cannot double -> hit
    CHECK(decide({"8S", "8D"}, "10C", true, true) == BjAction::Split);    // always split 8s
    CHECK(decide({"AS", "7D"}, "9C", true, false) == BjAction::Hit);      // soft 18 vs 9
    CHECK(decide({"10S", "10D"}, "6C", true, true) == BjAction::Stand);   // never split tens
}

TEST(red_dog_payout_table) {
    CHECK_EQ(redDogPayout(1), 5);
    CHECK_EQ(redDogPayout(2), 4);
    CHECK_EQ(redDogPayout(3), 2);
    CHECK_EQ(redDogPayout(4), 1);
    CHECK_EQ(redDogPayout(11), 1);
}

TEST(ledger_conserves_chips_and_rejects_bad_transfers) {
    Ledger ledger(1000);
    ledger.openAccount("alice");
    ledger.transfer(Ledger::HOUSE, "alice", 300, "grant");
    CHECK_EQ(ledger.balance("alice"), 300L);
    CHECK_EQ(ledger.balance(Ledger::HOUSE), 700L);
    CHECK(ledger.audit());
    CHECK_THROWS(InsufficientFundsException, ledger.transfer("alice", Ledger::HOUSE, 301, "too much"));
    CHECK_EQ(ledger.balance("alice"), 300L);  // unchanged after the failed transfer
    CHECK_THROWS(UnknownAccountException, ledger.balance("nobody"));
    CHECK_THROWS(InvalidBetException, ledger.transfer("alice", Ledger::HOUSE, -5, "negative"));
    const long before = ledger.transactionCount();
    ledger.transfer("alice", Ledger::HOUSE, 0, "zero is a no-op");
    CHECK_EQ(ledger.transactionCount(), before);
    CHECK(ledger.audit());
}

TEST(templates_and_console_parsing) {
    Leaderboard<long> board;
    board.set("bob", 50);
    board.set("alice", 90);
    board.set("carol", 50);
    const auto top = board.top(2);
    CHECK_EQ(top.size(), static_cast<std::size_t>(2));
    CHECK_EQ(top[0].first, std::string("alice"));
    CHECK_EQ(top[1].first, std::string("bob"));  // ties break alphabetically
    CHECK_EQ(maxOf(std::vector<int>{3, 9, 4}), 9);
    CHECK_THROWS(std::invalid_argument, maxOf(std::vector<int>{}));
    CHECK_EQ(clampValue(15, 0, 10), 10);
    CHECK_EQ(console::parseLong(" 42 "), 42L);
    CHECK_THROWS(InvalidInputException, console::parseLong("4x2"));
    CHECK_THROWS(InvalidInputException, console::parseLong(""));
}

TEST(profile_text_round_trip_and_validation) {
    PlayerProfile p("Alice", 1200);
    p.recordResult(50).recordResult(-20);  // chained through *this
    std::stringstream ss;
    ss << p;
    PlayerProfile back;
    ss >> back;
    CHECK(static_cast<bool>(ss));
    CHECK_EQ(back.name(), std::string("Alice"));
    CHECK_EQ(back.hands(), 2L);
    CHECK_EQ(back.wins(), 1L);
    CHECK_EQ(back.lifetimeNet(), 30L);
    std::stringstream bad("bad*name 10 0 0 0 0");
    PlayerProfile rejected;
    bad >> rejected;
    CHECK(bad.fail());
    CHECK(!isValidPlayerName("HOUSE"));
    CHECK(!isValidPlayerName("a"));
}

TEST(storage_profiles_round_trip_and_detect_tampering) {
    std::filesystem::remove_all(kTestDir);
    Storage storage(kTestDir);
    std::map<std::string, PlayerProfile> profiles;
    profiles["alice"] = PlayerProfile("Alice", 1500);
    profiles["bob"] = PlayerProfile("Bob", 700);
    storage.saveProfiles(profiles);
    const auto loaded = storage.loadProfiles();
    CHECK_EQ(loaded.size(), static_cast<std::size_t>(2));
    CHECK(loaded.count("alice") == 1);  // keyed by lower-case name, like CasinoCore
    if (loaded.count("alice")) CHECK_EQ(loaded.at("alice").balance(), 1500L);

    {  // edit a balance by hand: the checksum must catch it
        std::ifstream in(storage.profilesPath(), std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        in.close();
        std::string s = text.str();
        s.replace(s.find("1500"), 4, "9999");
        std::ofstream out(storage.profilesPath(), std::ios::binary | std::ios::trunc);
        out << s;
    }
    CHECK_THROWS(DataCorruptionException, storage.loadProfiles());
}

TEST(returning_player_keeps_balance_across_sessions) {
    std::filesystem::remove_all(kTestDir);
    {
        CasinoCore first(kTestDir, nullptr);
        BotPlayer alice("Alice", BotPlayer::Style::Steady, 1);
        first.enroll(alice);
        first.ledger().transfer(alice.account(), Ledger::HOUSE, 250, "test loss");  // balance 750
        first.shutdown();
    }
    CasinoCore second(kTestDir, nullptr);
    CHECK(second.notices().empty());  // no corruption warning on reload
    BotPlayer alice("Alice", BotPlayer::Style::Steady, 1);
    const std::string message = second.enroll(alice);
    CHECK(message.find("Welcome back") != std::string::npos);
    CHECK_EQ(second.balanceOf(alice), 750L);
    CHECK_EQ(second.profiles().size(), static_cast<std::size_t>(1));
    second.shutdown();
    std::filesystem::remove_all(kTestDir);
}

TEST(storage_binary_history_round_trip_and_detect_corruption) {
    std::filesystem::remove_all(kTestDir);
    Storage storage(kTestDir);
    HistoryRecord a;
    a.timestamp = 1700000000;
    a.round = 3;
    a.game = 2;
    a.player = "Alice";
    a.net = -40;
    a.balanceAfter = 960;
    HistoryRecord b = a;
    b.player = "Bob";
    b.net = 40;
    storage.appendHistory({a, b});
    const auto loaded = storage.loadHistory();
    CHECK_EQ(loaded.size(), static_cast<std::size_t>(2));
    CHECK_EQ(loaded[0].player, std::string("Alice"));
    CHECK_EQ(loaded[1].net, 40L);
    CHECK_EQ(loaded[0].balanceAfter, 960L);

    {  // flip one byte inside the first record
        std::fstream f(storage.historyPath(), std::ios::in | std::ios::out | std::ios::binary);
        f.seekp(16 + 20);
        f.put('X');
    }
    CHECK_THROWS(DataCorruptionException, storage.loadHistory());
    std::filesystem::remove_all(kTestDir);
}

TEST(poker_scripted_fold_gives_blinds_to_big_blind) {
    Ledger ledger(100000);
    FairnessService fairness;
    GameContext ctx{ledger, fairness, nullptr};
    PokerGame poker(ctx);
    ScriptedPlayer folder("Folder", PokerKind::Fold);   // seat 0 = button = small blind heads-up
    ScriptedPlayer caller("Caller", PokerKind::Check);
    for (Player* p : {static_cast<Player*>(&folder), static_cast<Player*>(&caller)}) {
        ledger.openAccount(p->account());
        ledger.transfer(Ledger::HOUSE, p->account(), 500, "test chips");
    }
    std::vector<Player*> seated = {&folder, &caller};
    const RoundResult r = poker.playRound(seated);
    CHECK(r.played);
    CHECK_EQ(r.net.at("Folder"), -5L);
    CHECK_EQ(r.net.at("Caller"), 5L);
    CHECK_EQ(ledger.balance(Ledger::RAKE), 0L);  // no flop, no rake
    CHECK_EQ(ledger.balance("table:Poker"), 0L);
    CHECK(ledger.audit());
}

TEST(poker_all_in_showdown_conserves_chips) {
    Ledger ledger(100000);
    FairnessService fairness;
    GameContext ctx{ledger, fairness, nullptr};
    PokerGame poker(ctx);
    ScriptedPlayer a("Shover", PokerKind::Raise), b("CallerA", PokerKind::Check), c("CallerB", PokerKind::Check);
    std::vector<Player*> seated = {&a, &b, &c};
    const long stacks[3] = {300, 150, 600};  // different stacks -> side pots
    for (int i = 0; i < 3; ++i) {
        ledger.openAccount(seated[i]->account());
        ledger.transfer(Ledger::HOUSE, seated[i]->account(), stacks[i], "test chips");
    }
    for (int hand = 0; hand < 25; ++hand) {
        for (Player* p : seated) {
            const long bal = ledger.balance(p->account());
            if (bal < 200) ledger.transfer(Ledger::HOUSE, p->account(), 200 - bal, "top up");
        }
        const RoundResult r = poker.playRound(seated);
        CHECK(r.played);
        long net = 0;
        for (const auto& e : r.net) net += e.second;
        CHECK(net <= 0);  // players never gain chips in total: at most the rake leaves the table
        CHECK_EQ(ledger.balance("table:Poker"), 0L);
    }
    CHECK(ledger.audit());
    CHECK_EQ(poker.tableJson().find("\"cards\":[\"") != std::string::npos, true);  // showdown cards are public
}

TEST(blackjack_and_red_dog_rounds_keep_ledger_balanced) {
    Ledger ledger(100000);
    FairnessService fairness;
    GameContext ctx{ledger, fairness, nullptr};
    BlackjackGame blackjack(ctx);
    RedDogGame redDog(ctx);
    ScriptedPlayer p1("Stander", PokerKind::Check, 20);
    BotPlayer p2("Bot", BotPlayer::Style::Steady, 7);
    std::vector<Player*> seated = {&p1, &p2};
    for (Player* p : seated) {
        ledger.openAccount(p->account());
        ledger.transfer(Ledger::HOUSE, p->account(), 5000, "test chips");
    }
    for (int i = 0; i < 200; ++i) {
        CHECK(blackjack.playRound(seated).played);
        CHECK(redDog.playRound(seated).played);
        CHECK_EQ(ledger.balance("table:Blackjack"), 0L);
        CHECK_EQ(ledger.balance("table:Red Dog"), 0L);
    }
    CHECK(ledger.audit());
    CHECK(Game::totalRoundsAllGames() >= 400);
}

TEST(bot_simulations_finish_with_balanced_ledgers) {
    std::ostringstream out;
    CHECK_EQ(runSimulation("blackjack", 2000, out), 0);
    CHECK_EQ(runSimulation("reddog", 2000, out), 0);
    CHECK_EQ(runSimulation("poker", 60, out), 0);
    CHECK_THROWS(InvalidInputException, runSimulation("roulette", 1, out));
}

// ================================================================== main
int main() {
    int passed = 0;
    for (const TestCase& test : registry()) {
        g_current = test.name;
        const int before = g_failures;
        try {
            test.body();
        } catch (const std::exception& e) {
            fail(std::string("unexpected exception: ") + e.what(), 0);
        }
        const bool ok = g_failures == before;
        if (ok) ++passed;
        std::cout << (ok ? "  PASS  " : "  FAIL  ") << test.name << '\n';
    }
    std::cout << '\n' << passed << " of " << registry().size() << " tests passed";
    if (g_failures) std::cout << ", " << g_failures << " failed checks";
    std::cout << '\n';
    return g_failures == 0 ? 0 : 1;
}
