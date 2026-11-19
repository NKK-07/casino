// WebSession.hpp - connects browsers to the casino engine, with rooms.
//
//   WebSession  the lobby: who is connected and which rooms exist
//   Room        one private table: its own three Game objects, round thread,
//               log and computer players. Rooms share the ledger and profiles.
//   WebPlayer   a Player whose decisions come from a browser
//
// A round runs on its room's worker thread. When the engine needs a decision
// from a browser player it calls WebPlayer::pokerAction() (or the Blackjack /
// Red Dog equivalent) exactly as for a bot or a console player; the call
// blocks until the browser answers through the HTTP API. One mutex guards all
// shared state; a worker holds it while the engine runs and releases it while
// it waits for a player, so many rooms can play at the same time.
#pragma once

#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <ostream>
#include <streambuf>
#include <string>
#include <thread>
#include <vector>

#include "CasinoCore.hpp"

namespace casino {

class Room;

// Synchronisation shared by every HTTP thread and every room's worker thread.
struct WebShared {
    std::mutex mutex;
    std::condition_variable cv;
    int decisionSeconds = 90;  // time a browser player gets per decision
    bool shuttingDown = false;
};

// A player controlled from a web browser.
class WebPlayer : public Player {
public:
    WebPlayer(std::string name, std::string token, WebShared& shared);

    bool isHuman() const override { return true; }
    long chooseBet(const BetView& view) override;
    BjAction blackjackAction(const BlackjackView& view) override;
    long redDogRaise(const RedDogView& view) override;
    PokerMove pokerAction(const PokerView& view) override;

    // Everything below is called with the shared mutex held.
    const std::string& token() const { return token_; }
    void setToken(const std::string& token) { token_ = token; }
    void touch() { lastSeen_ = std::chrono::steady_clock::now(); }
    bool present() const { return secondsAway() < 20; }
    long secondsAway() const;
    void forgetPresence() { lastSeen_ = std::chrono::steady_clock::time_point(); }

    Room* room() const { return room_; }
    void setRoom(Room* room) { room_ = room; }
    void setWorkerLock(std::unique_lock<std::mutex>* lock) { lock_ = lock; }

    bool hasPrompt() const { return active_; }
    long promptId() const { return promptId_; }
    const std::string& promptKind() const { return kind_; }
    const std::string& promptJson() const { return json_; }
    bool submit(long promptId, const std::string& answer);  // false when stale

private:
    // Publishes a prompt and blocks (releasing the mutex) until the browser
    // answers, leaves the room or the time limit passes. "" if unanswered.
    std::string ask(const std::string& kind, const std::string& payload);

    std::string token_;
    WebShared& shared_;
    Room* room_ = nullptr;
    std::unique_lock<std::mutex>* lock_ = nullptr;  // the room worker's lock during a round
    std::chrono::steady_clock::time_point lastSeen_;
    bool active_ = false;
    bool answered_ = false;
    long promptId_ = 0;
    std::string kind_;
    std::string json_;
    std::string answer_;
};

// Turns characters written to an ostream into whole log lines.
class LineBuffer : public std::streambuf {
public:
    explicit LineBuffer(std::function<void(const std::string&)> sink) : sink_(std::move(sink)) {}

protected:
    int_type overflow(int_type c) override;
    std::streamsize xsputn(const char* s, std::streamsize n) override;

private:
    std::function<void(const std::string&)> sink_;
    std::string line_;
};

// One private table.
class Room {
public:
    static constexpr std::size_t kMaxSeats = 6;
    static constexpr std::size_t kMaxLogLines = 800;

    Room(std::string code, std::string name, std::string host, CasinoCore& core, WebShared& shared);
    ~Room();  // waits for a finished round thread

    Room(const Room&) = delete;
    Room& operator=(const Room&) = delete;

    const std::string& code() const { return code_; }
    const std::string& name() const { return name_; }
    bool running() const { return running_; }
    bool empty() const { return members_.empty(); }
    std::size_t memberCount() const { return members_.size(); }
    bool isSeated(const std::string& playerName) const;

    void addMember(WebPlayer& player);
    void removeMember(WebPlayer& player);
    const std::vector<WebPlayer*>& members() const { return members_; }

    void appendLog(const std::string& line);
    std::string summaryJson() const;                          // one row of the lobby list
    std::string stateJson(const WebPlayer& me, long since) const;  // the table as this player sees it
    std::string start(WebPlayer& starter, GameKind kind, int bots);  // "" or an error message

private:
    void runRound(GameKind kind, std::vector<Player*> seated);
    std::string playersJson() const;

    CasinoCore& core_;
    WebShared& shared_;
    std::string code_;
    std::string name_;
    std::string host_;
    LineBuffer lineBuffer_;
    std::ostream logStream_;
    GameContext ctx_;  // this room's table talk goes to its own log
    std::vector<std::unique_ptr<Game>> games_;
    std::vector<WebPlayer*> members_;  // owned by WebSession
    std::vector<std::unique_ptr<BotPlayer>> bots_;
    std::deque<std::string> log_;
    long logStart_ = 0;
    std::thread worker_;
    bool running_ = false;
    bool hasGame_ = false;
    GameKind currentKind_ = GameKind::Poker;
    std::string currentGame_;
    std::vector<std::string> seatedNames_;
    int roundCounter_ = 0;
};

class WebSession {
public:
    static constexpr std::size_t kMaxRooms = 50;

    explicit WebSession(CasinoCore& core);
    ~WebSession();

    WebSession(const WebSession&) = delete;
    WebSession& operator=(const WebSession&) = delete;

    // Every function returns a JSON document: {"ok":true,...} or {"ok":false,"error":"..."}.
    std::string join(const std::string& name, const std::string& token);
    std::string leave(const std::string& name, const std::string& token);
    std::string state(const std::string& name, const std::string& token, long since);
    std::string createRoom(const std::string& name, const std::string& token, const std::string& roomName);
    std::string joinRoom(const std::string& name, const std::string& token, const std::string& code);
    std::string leaveRoom(const std::string& name, const std::string& token);
    std::string startRound(const std::string& name, const std::string& token, const std::string& game, int bots);
    std::string act(const std::string& name, const std::string& token, long promptId, const std::string& answer);
    std::string leaderboard();
    std::string history();

    void shutdown();

private:
    static std::string error(const std::string& message);
    static std::string keyOf(const std::string& name);
    WebPlayer* find(const std::string& name, const std::string& token);
    void exitRoom(WebPlayer& player);
    void prune();  // drops long-gone members and empty idle rooms
    std::string roomsJson() const;
    std::string newRoomCode();
    std::string newToken(const std::string& name);

    CasinoCore& core_;
    WebShared shared_;
    std::map<std::string, std::unique_ptr<WebPlayer>> humans_;  // keyed by lower-case name
    std::map<std::string, std::unique_ptr<Room>> rooms_;        // keyed by room code
    bool closed_ = false;
};

}  // namespace casino
