#include "WebSession.hpp"

#include <algorithm>
#include <cctype>
#include <random>
#include <sstream>

#include "Console.hpp"
#include "Exceptions.hpp"
#include "Json.hpp"
#include "Sha256.hpp"
#include "Templates.hpp"

namespace casino {
namespace {

const char* streetWord(Street s) {
    switch (s) {
        case Street::PreFlop: return "pre-flop";
        case Street::Flop: return "flop";
        case Street::Turn: return "turn";
        case Street::River: return "river";
    }
    return "";
}

long parseOr(const std::string& text, long fallback) {
    try {
        return console::parseLong(text);
    } catch (const InvalidInputException&) {
        return fallback;
    }
}

std::string trim(const std::string& s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

}  // namespace

// ================================================================ WebPlayer

WebPlayer::WebPlayer(std::string name, std::string token, WebShared& shared)
    : Player(std::move(name)), token_(std::move(token)), shared_(shared) {
    touch();
}

long WebPlayer::secondsAway() const {
    return static_cast<long>(
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - lastSeen_).count());
}

bool WebPlayer::submit(long promptId, const std::string& answer) {
    if (!active_ || answered_ || promptId != promptId_) return false;
    answer_ = answer;
    answered_ = true;
    return true;
}

std::string WebPlayer::ask(const std::string& kind, const std::string& payload) {
    Room* const askingRoom = room_;
    if (lock_ == nullptr || shared_.shuttingDown || askingRoom == nullptr) return "";
    if (!present()) {
        askingRoom->appendLog("  " + name() + " is away - default action taken.");
        return "";
    }
    ++promptId_;
    kind_ = kind;
    json_ = payload;
    answer_.clear();
    answered_ = false;
    active_ = true;

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(shared_.decisionSeconds);
    while (!answered_ && !shared_.shuttingDown && present() && room_ == askingRoom &&
           std::chrono::steady_clock::now() < deadline) {
        shared_.cv.wait_for(*lock_, std::chrono::milliseconds(1000));  // releases the mutex while waiting
    }
    active_ = false;
    if (!answered_) {
        askingRoom->appendLog("  " + name() + " did not answer in time - default action taken.");
        return "";
    }
    return answer_;
}

long WebPlayer::chooseBet(const BetView& v) {
    std::ostringstream o;
    o << "{\"game\":" << json::quote(v.game) << ",\"min\":" << v.minBet << ",\"max\":" << v.maxBet
      << ",\"balance\":" << v.balance << "}";
    return parseOr(ask("bet", o.str()), 0);  // unanswered = sit out
}

BjAction WebPlayer::blackjackAction(const BlackjackView& v) {
    std::ostringstream o;
    o << "{\"cards\":" << json::cards(v.cards) << ",\"dealer\":" << json::quote(v.dealerUp.str())
      << ",\"total\":" << v.total << ",\"soft\":" << json::boolean(v.soft) << ",\"stake\":" << v.stake
      << ",\"handIndex\":" << v.handIndex << ",\"handCount\":" << v.handCount
      << ",\"canDouble\":" << json::boolean(v.canDouble) << ",\"canSplit\":" << json::boolean(v.canSplit) << "}";
    const std::string answer = ask("blackjack", o.str());
    if (answer == "hit") return BjAction::Hit;
    if (answer == "double") return BjAction::Double;
    if (answer == "split") return BjAction::Split;
    return BjAction::Stand;  // "stand" and unanswered
}

long WebPlayer::redDogRaise(const RedDogView& v) {
    if (v.maxRaise <= 0) return 0;
    std::ostringstream o;
    o << "{\"low\":" << json::quote(v.low.str()) << ",\"high\":" << json::quote(v.high.str())
      << ",\"spread\":" << v.spread << ",\"ante\":" << v.ante << ",\"maxRaise\":" << v.maxRaise
      << ",\"payout\":" << v.payout << "}";
    return parseOr(ask("reddog", o.str()), 0);
}

PokerMove WebPlayer::pokerAction(const PokerView& v) {
    std::ostringstream o;
    o << "{\"hole\":" << json::cards(v.hole) << ",\"board\":" << json::cards(v.board) << ",\"street\":"
      << json::quote(streetWord(v.street)) << ",\"pot\":" << v.pot << ",\"toCall\":" << v.toCall
      << ",\"stack\":" << v.stack << ",\"minRaiseTo\":" << v.minRaiseTo << ",\"maxRaiseTo\":" << v.maxRaiseTo
      << ",\"canRaise\":" << json::boolean(v.canRaise) << ",\"opponents\":" << v.opponentsInHand << "}";
    const std::string answer = ask("poker", o.str());

    PokerMove move;
    if (answer == "c") {
        move.kind = v.toCall == 0 ? PokerKind::Check : PokerKind::Call;
    } else if (answer.rfind("r:", 0) == 0) {
        move.kind = PokerKind::Raise;
        move.raiseTo = parseOr(answer.substr(2), v.minRaiseTo);
    } else if (answer == "f") {
        move.kind = PokerKind::Fold;
    } else {
        move.kind = v.toCall == 0 ? PokerKind::Check : PokerKind::Fold;  // unanswered: never pay for a hand
    }
    return move;
}

// ================================================================ LineBuffer

LineBuffer::int_type LineBuffer::overflow(int_type c) {
    if (traits_type::eq_int_type(c, traits_type::eof())) return traits_type::not_eof(c);
    const char ch = traits_type::to_char_type(c);
    if (ch == '\n') {
        sink_(line_);
        line_.clear();
    } else if (ch != '\r') {
        line_.push_back(ch);
    }
    return c;
}

std::streamsize LineBuffer::xsputn(const char* s, std::streamsize n) {
    for (std::streamsize i = 0; i < n; ++i) overflow(traits_type::to_int_type(s[i]));
    return n;
}

// ================================================================ Room

Room::Room(std::string code, std::string name, std::string host, CasinoCore& core, WebShared& shared)
    : core_(core),
      shared_(shared),
      code_(std::move(code)),
      name_(std::move(name)),
      host_(std::move(host)),
      lineBuffer_([this](const std::string& line) { appendLog(line); }),
      logStream_(&lineBuffer_),
      ctx_{core.ledger(), core.fairness(), &logStream_} {
    // Each room gets its own game objects, with table accounts tagged by the room code.
    for (GameKind kind : {GameKind::Blackjack, GameKind::RedDog, GameKind::Poker}) {
        games_.push_back(CasinoCore::makeGame(kind, ctx_, code_));
    }
}

Room::~Room() {
    if (worker_.joinable()) worker_.join();
}

bool Room::isSeated(const std::string& playerName) const {
    return running_ && std::find(seatedNames_.begin(), seatedNames_.end(), playerName) != seatedNames_.end();
}

void Room::addMember(WebPlayer& player) {
    if (std::find(members_.begin(), members_.end(), &player) == members_.end()) members_.push_back(&player);
    player.setRoom(this);
    appendLog(player.name() + " joined the room.");
}

void Room::removeMember(WebPlayer& player) {
    members_.erase(std::remove(members_.begin(), members_.end(), &player), members_.end());
    if (player.room() == this) player.setRoom(nullptr);
    appendLog(player.name() + " left the room.");
}

void Room::appendLog(const std::string& line) {
    log_.push_back(line);
    if (log_.size() > kMaxLogLines) {
        log_.pop_front();
        ++logStart_;
    }
}

std::string Room::summaryJson() const {
    std::ostringstream o;
    o << "{\"code\":" << json::quote(code_) << ",\"name\":" << json::quote(name_) << ",\"host\":" << json::quote(host_)
      << ",\"players\":" << members_.size() << ",\"running\":" << json::boolean(running_)
      << ",\"game\":" << json::quote(running_ ? currentGame_ : std::string()) << "}";
    return o.str();
}

std::string Room::playersJson() const {
    std::vector<std::string> items;
    for (const WebPlayer* p : members_) {
        std::ostringstream o;
        o << "{\"name\":" << json::quote(p->name()) << ",\"balance\":" << core_.balanceOf(*p)
          << ",\"human\":true,\"present\":" << json::boolean(p->present())
          << ",\"seated\":" << json::boolean(isSeated(p->name())) << "}";
        items.push_back(o.str());
    }
    for (const auto& bot : bots_) {
        std::ostringstream o;
        o << "{\"name\":" << json::quote(bot->name()) << ",\"balance\":" << core_.balanceOf(*bot)
          << ",\"human\":false,\"present\":true,\"seated\":" << json::boolean(isSeated(bot->name())) << "}";
        items.push_back(o.str());
    }
    return json::array(items);
}

std::string Room::stateJson(const WebPlayer& me, long since) const {
    const long from = std::max(since, logStart_);
    std::vector<std::string> lines;
    for (long i = from; i < logStart_ + static_cast<long>(log_.size()); ++i) {
        lines.push_back(log_[static_cast<std::size_t>(i - logStart_)]);
    }
    std::ostringstream o;
    o << "{\"ok\":true,\"me\":{\"name\":" << json::quote(me.name()) << ",\"balance\":" << core_.balanceOf(me) << "}"
      << ",\"room\":{\"code\":" << json::quote(code_) << ",\"name\":" << json::quote(name_)
      << ",\"host\":" << json::quote(host_) << "}"
      << ",\"round\":{\"running\":" << json::boolean(running_) << ",\"game\":" << json::quote(currentGame_)
      << ",\"number\":" << roundCounter_ << ",\"seated\":" << json::strings(seatedNames_) << "}"
      << ",\"players\":" << playersJson()
      << ",\"table\":" << (hasGame_ ? games_[static_cast<std::size_t>(currentKind_)]->tableJson() : std::string("null"))
      << ",\"log\":{\"from\":" << from << ",\"next\":" << logStart_ + static_cast<long>(log_.size())
      << ",\"lines\":" << json::strings(lines) << "}"
      << ",\"prompt\":";
    if (me.hasPrompt()) {
        o << "{\"id\":" << me.promptId() << ",\"kind\":" << json::quote(me.promptKind()) << ",\"data\":" << me.promptJson()
          << ",\"seconds\":" << shared_.decisionSeconds << "}";
    } else {
        o << "null";
    }
    o << "}";
    return o.str();
}

std::string Room::start(WebPlayer& starter, GameKind kind, int bots) {
    if (running_) return "A round is already in progress.";
    if (worker_.joinable()) worker_.join();  // the previous round has finished

    std::vector<Player*> seated;
    for (WebPlayer* p : members_) {
        if (p->present() && seated.size() < kMaxSeats) {
            core_.enroll(*p);  // tops up anyone who ran low
            seated.push_back(p);
        }
    }

    // Computer players get names tagged with the room code, so every room has its own.
    // "Bot_" + name + "_" + 5-letter code must stay within the 16-character name limit.
    static const char* const botNames[] = {"Ada", "Alan", "Grace", "Linus", "Ken"};
    static const BotPlayer::Style botStyles[] = {BotPlayer::Style::Steady, BotPlayer::Style::Tight,
                                                 BotPlayer::Style::Aggressive, BotPlayer::Style::Steady,
                                                 BotPlayer::Style::Tight};
    bots = std::max(0, std::min(bots, 5));
    while (static_cast<int>(bots_.size()) < bots) {
        const std::size_t i = bots_.size();
        auto bot = std::make_unique<BotPlayer>(std::string("Bot_") + botNames[i] + "_" + code_, botStyles[i]);
        bot->setEquityTrials(100);
        bots_.push_back(std::move(bot));
    }
    for (int i = 0; i < bots && seated.size() < kMaxSeats; ++i) {
        core_.enroll(*bots_[static_cast<std::size_t>(i)]);
        seated.push_back(bots_[static_cast<std::size_t>(i)].get());
    }

    Game& g = *games_[static_cast<std::size_t>(kind)];
    if (static_cast<int>(seated.size()) < g.minPlayers()) {
        return g.name() + " needs at least " + std::to_string(g.minPlayers()) +
               " players - add a computer opponent or wait for a friend.";
    }

    seatedNames_.clear();
    for (Player* p : seated) seatedNames_.push_back(p->name());
    currentGame_ = g.name();
    currentKind_ = kind;
    hasGame_ = true;
    running_ = true;
    ++roundCounter_;
    appendLog("========== " + g.name() + " - started by " + starter.name() + " ==========");
    worker_ = std::thread(&Room::runRound, this, kind, std::move(seated));
    return "";
}

void Room::runRound(GameKind kind, std::vector<Player*> seated) {
    std::unique_lock<std::mutex> lock(shared_.mutex);  // held except while waiting for a player
    std::vector<WebPlayer*> webSeats;
    for (WebPlayer* p : members_) {
        if (std::find(seated.begin(), seated.end(), p) != seated.end()) {
            p->setWorkerLock(&lock);
            webSeats.push_back(p);
        }
    }
    try {
        core_.playRound(*games_[static_cast<std::size_t>(kind)], kind, seated);
    } catch (const std::exception& e) {
        appendLog(std::string("The round was aborted: ") + e.what());
    }
    for (WebPlayer* p : webSeats) p->setWorkerLock(nullptr);
    appendLog("---------- round finished ----------");
    running_ = false;
    for (const std::string& note : core_.notices()) appendLog("[!] " + note);
    core_.clearNotices();
}

// ================================================================ WebSession

WebSession::WebSession(CasinoCore& core) : core_(core) {}

WebSession::~WebSession() { shutdown(); }

void WebSession::shutdown() {
    std::map<std::string, std::unique_ptr<Room>> closing;
    {
        std::lock_guard<std::mutex> lock(shared_.mutex);
        if (closed_) return;
        closed_ = true;
        shared_.shuttingDown = true;
        closing.swap(rooms_);
    }
    shared_.cv.notify_all();
    closing.clear();  // each Room waits for its round thread, which now finishes with default moves
}

std::string WebSession::error(const std::string& message) {
    return "{\"ok\":false,\"error\":" + json::quote(message) + "}";
}

std::string WebSession::keyOf(const std::string& name) {
    std::string key;
    for (char c : name) key.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return key;
}

std::string WebSession::newToken(const std::string& name) {
    std::random_device rd;
    std::ostringstream t;
    t << std::hex << rd() << rd() << rd() << rd();
    return sha256Hex(name + ":" + t.str()).substr(0, 24);
}

std::string WebSession::newRoomCode() {
    static const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";  // no 0/O or 1/I confusion
    std::random_device rd;
    while (true) {
        std::string code;
        for (int i = 0; i < 5; ++i) code.push_back(alphabet[rd() % (sizeof(alphabet) - 1)]);
        if (rooms_.count(code) == 0) return code;
    }
}

WebPlayer* WebSession::find(const std::string& name, const std::string& token) {
    const auto it = humans_.find(keyOf(name));
    if (it == humans_.end() || token.empty() || it->second->token() != token) return nullptr;
    return it->second.get();
}

void WebSession::exitRoom(WebPlayer& player) {
    Room* room = player.room();
    if (room == nullptr) return;
    room->removeMember(player);
    shared_.cv.notify_all();  // a pending question of theirs ends with the default move
}

void WebSession::prune() {
    for (auto& entry : humans_) {
        WebPlayer& p = *entry.second;
        if (p.room() != nullptr && p.secondsAway() > 60 && !p.room()->isSeated(p.name())) exitRoom(p);
    }
    for (auto it = rooms_.begin(); it != rooms_.end();) {
        if (it->second->empty() && !it->second->running()) it = rooms_.erase(it);  // ~Room joins its finished thread
        else ++it;
    }
}

std::string WebSession::roomsJson() const {
    std::vector<std::string> items;
    for (const auto& entry : rooms_) items.push_back(entry.second->summaryJson());
    return json::array(items);
}

std::string WebSession::join(const std::string& rawName, const std::string& token) {
    std::lock_guard<std::mutex> lock(shared_.mutex);
    const std::string name = trim(rawName);
    if (!isValidPlayerName(name)) return error("Names are 2-16 letters, digits, '_' or '-'.");
    if (keyOf(name).rfind("bot_", 0) == 0) return error("Names starting with 'Bot_' are reserved for computer players.");

    std::string message;
    WebPlayer* player = nullptr;
    const auto existing = humans_.find(keyOf(name));
    if (existing != humans_.end()) {
        WebPlayer& p = *existing->second;
        if (p.token() != token && p.present()) return error("That name is in use by someone else right now.");
        if (p.token() != token) {
            p.setToken(newToken(name));  // the old browser is gone: this one takes the seat over
            message = "Welcome back, " + p.name() + "!";
        }
        p.touch();
        player = &p;
    } else {
        if (humans_.size() >= 200) return error("The casino is full right now.");
        auto created = std::make_unique<WebPlayer>(name, newToken(name), shared_);
        message = core_.enroll(*created);
        player = created.get();
        humans_[keyOf(name)] = std::move(created);
    }
    for (const std::string& note : core_.notices()) message += " " + note;
    core_.clearNotices();

    std::vector<std::string> games;
    for (const auto& g : core_.games()) {
        std::ostringstream o;
        const std::string key = g->name() == "Blackjack" ? "blackjack" : (g->name() == "Red Dog" ? "reddog" : "poker");
        o << "{\"key\":" << json::quote(key) << ",\"name\":" << json::quote(g->name()) << ",\"minBet\":" << g->minBet()
          << ",\"maxBet\":" << g->maxBet() << ",\"minPlayers\":" << g->minPlayers()
          << ",\"rules\":" << json::quote(g->rules()) << "}";
        games.push_back(o.str());
    }
    std::ostringstream o;
    o << "{\"ok\":true,\"name\":" << json::quote(player->name()) << ",\"token\":" << json::quote(player->token())
      << ",\"message\":" << json::quote(message) << ",\"games\":" << json::array(games) << "}";
    return o.str();
}

std::string WebSession::leave(const std::string& name, const std::string& token) {
    std::lock_guard<std::mutex> lock(shared_.mutex);
    WebPlayer* p = find(name, token);
    if (p != nullptr) {
        exitRoom(*p);
        p->forgetPresence();  // the object stays (a running round may still point at it)
        prune();
    }
    return "{\"ok\":true}";
}

std::string WebSession::state(const std::string& name, const std::string& token, long since) {
    std::lock_guard<std::mutex> lock(shared_.mutex);
    WebPlayer* me = find(name, token);
    if (me == nullptr) return error("You are not seated - please join again.");
    me->touch();
    prune();
    if (me->room() != nullptr) return me->room()->stateJson(*me, since);
    std::ostringstream o;
    o << "{\"ok\":true,\"me\":{\"name\":" << json::quote(me->name()) << ",\"balance\":" << core_.balanceOf(*me)
      << "},\"room\":null,\"rooms\":" << roomsJson() << "}";
    return o.str();
}

std::string WebSession::createRoom(const std::string& name, const std::string& token, const std::string& rawRoomName) {
    std::lock_guard<std::mutex> lock(shared_.mutex);
    WebPlayer* me = find(name, token);
    if (me == nullptr) return error("You are not seated - please join again.");
    me->touch();
    prune();
    std::string roomName = trim(rawRoomName);
    if (roomName.empty()) roomName = me->name() + "'s table";
    if (roomName.size() > 24) return error("Room names are at most 24 characters.");
    for (unsigned char c : roomName) {
        if (c < 0x20) return error("Room names cannot contain control characters.");
    }
    if (rooms_.size() >= kMaxRooms) return error("Too many rooms are open right now.");

    exitRoom(*me);
    const std::string code = newRoomCode();
    auto room = std::make_unique<Room>(code, roomName, me->name(), core_, shared_);
    room->addMember(*me);
    rooms_[code] = std::move(room);
    return "{\"ok\":true,\"code\":" + json::quote(code) + "}";
}

std::string WebSession::joinRoom(const std::string& name, const std::string& token, const std::string& rawCode) {
    std::lock_guard<std::mutex> lock(shared_.mutex);
    WebPlayer* me = find(name, token);
    if (me == nullptr) return error("You are not seated - please join again.");
    me->touch();
    prune();
    std::string code;
    for (char c : trim(rawCode)) code.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    const auto it = rooms_.find(code);
    if (it == rooms_.end()) return error("No room with code '" + code + "'.");
    Room& room = *it->second;
    if (me->room() == &room) return "{\"ok\":true,\"code\":" + json::quote(code) + "}";
    if (room.memberCount() >= Room::kMaxSeats) return error("That room is full (6 players).");
    exitRoom(*me);
    room.addMember(*me);
    return "{\"ok\":true,\"code\":" + json::quote(code) + "}";
}

std::string WebSession::leaveRoom(const std::string& name, const std::string& token) {
    std::lock_guard<std::mutex> lock(shared_.mutex);
    WebPlayer* me = find(name, token);
    if (me == nullptr) return error("You are not seated - please join again.");
    me->touch();
    exitRoom(*me);
    prune();
    return "{\"ok\":true}";
}

std::string WebSession::startRound(const std::string& name, const std::string& token, const std::string& game,
                                   int bots) {
    std::lock_guard<std::mutex> lock(shared_.mutex);
    WebPlayer* me = find(name, token);
    if (me == nullptr) return error("You are not seated - please join again.");
    me->touch();
    if (me->room() == nullptr) return error("Join or create a room first.");
    GameKind kind;
    try {
        kind = parseGameKind(game);
    } catch (const InvalidInputException& e) {
        return error(e.what());
    }
    const std::string problem = me->room()->start(*me, kind, bots);
    return problem.empty() ? "{\"ok\":true}" : error(problem);
}

std::string WebSession::act(const std::string& name, const std::string& token, long promptId,
                            const std::string& answer) {
    std::lock_guard<std::mutex> lock(shared_.mutex);
    WebPlayer* me = find(name, token);
    if (me == nullptr) return error("You are not seated - please join again.");
    me->touch();
    if (!me->submit(promptId, answer)) return error("That question has already been answered or expired.");
    shared_.cv.notify_all();
    return "{\"ok\":true}";
}

std::string WebSession::leaderboard() {
    std::lock_guard<std::mutex> lock(shared_.mutex);
    const auto encode = [](const std::vector<std::pair<std::string, long>>& entries) {
        std::vector<std::string> items;
        for (const auto& e : entries) {
            items.push_back("{\"name\":" + json::quote(e.first) + ",\"value\":" + std::to_string(e.second) + "}");
        }
        return json::array(items);
    };
    return "{\"ok\":true,\"richest\":" + encode(core_.balanceBoard().top(10)) +
           ",\"winners\":" + encode(core_.winningsBoard().top(10)) +
           ",\"rake\":" + std::to_string(core_.ledger().balance(Ledger::RAKE)) + "}";
}

std::string WebSession::history() {
    std::lock_guard<std::mutex> lock(shared_.mutex);
    std::vector<HistoryRecord> all;
    try {
        all = core_.storage().loadHistory();
    } catch (const CasinoException& e) {
        return error(e.what());
    }
    std::vector<std::string> items;
    const std::size_t from = all.size() > 25 ? all.size() - 25 : 0;
    for (std::size_t i = all.size(); i-- > from;) {  // newest first
        const HistoryRecord& r = all[i];
        std::ostringstream o;
        o << "{\"time\":" << r.timestamp << ",\"game\":" << json::quote(historyGameName(r.game)) << ",\"round\":"
          << r.round << ",\"player\":" << json::quote(r.player) << ",\"net\":" << r.net << ",\"balance\":"
          << r.balanceAfter << "}";
        items.push_back(o.str());
    }
    return "{\"ok\":true,\"total\":" + std::to_string(all.size()) + ",\"records\":" + json::array(items) + "}";
}

}  // namespace casino
