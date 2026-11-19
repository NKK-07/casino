#include "WebApp.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

#include "CasinoCore.hpp"
#include "Exceptions.hpp"
#include "HttpServer.hpp"
#include "WebSession.hpp"

namespace casino {
namespace {

std::atomic<bool> g_stopRequested{false};

void onSignal(int) { g_stopRequested = true; }

HttpResponse requirePost(const HttpRequest& req) {
    HttpResponse r = HttpResponse::json("{\"ok\":false,\"error\":\"use POST for this endpoint\"}", 405);
    (void)req;
    return r;
}

}  // namespace

int runWebServer(const WebOptions& options) {
    try {
        CasinoCore core(options.dataDir, nullptr);
        WebSession session(core);
        HttpServer server(options.host, options.port, options.webDir);

        // POST routes change state; GET routes only read it.
        server.route("/api/join", [&](const HttpRequest& r) {
            if (r.method != "POST") return requirePost(r);
            return HttpResponse::json(session.join(r.param("name"), r.param("token")));
        });
        server.route("/api/leave", [&](const HttpRequest& r) {
            if (r.method != "POST") return requirePost(r);
            return HttpResponse::json(session.leave(r.param("name"), r.param("token")));
        });
        server.route("/api/room/create", [&](const HttpRequest& r) {
            if (r.method != "POST") return requirePost(r);
            return HttpResponse::json(session.createRoom(r.param("name"), r.param("token"), r.param("room")));
        });
        server.route("/api/room/join", [&](const HttpRequest& r) {
            if (r.method != "POST") return requirePost(r);
            return HttpResponse::json(session.joinRoom(r.param("name"), r.param("token"), r.param("code")));
        });
        server.route("/api/room/leave", [&](const HttpRequest& r) {
            if (r.method != "POST") return requirePost(r);
            return HttpResponse::json(session.leaveRoom(r.param("name"), r.param("token")));
        });
        server.route("/api/start", [&](const HttpRequest& r) {
            if (r.method != "POST") return requirePost(r);
            int bots = 0;
            try {
                bots = std::stoi(r.param("bots", "0"));
            } catch (const std::exception&) {
                bots = 0;
            }
            return HttpResponse::json(session.startRound(r.param("name"), r.param("token"), r.param("game"), bots));
        });
        server.route("/api/act", [&](const HttpRequest& r) {
            if (r.method != "POST") return requirePost(r);
            long id = -1;
            try {
                id = std::stol(r.param("id", "-1"));
            } catch (const std::exception&) {
                id = -1;
            }
            return HttpResponse::json(session.act(r.param("name"), r.param("token"), id, r.param("answer")));
        });
        server.route("/api/state", [&](const HttpRequest& r) {
            long since = 0;
            try {
                since = std::stol(r.param("since", "0"));
            } catch (const std::exception&) {
                since = 0;
            }
            return HttpResponse::json(session.state(r.param("name"), r.param("token"), since));
        });
        server.route("/api/leaderboard", [&](const HttpRequest&) { return HttpResponse::json(session.leaderboard()); });
        server.route("/api/history", [&](const HttpRequest&) { return HttpResponse::json(session.history()); });

        std::signal(SIGINT, onSignal);
        std::signal(SIGTERM, onSignal);
        server.start();

        std::cout << "\n  OOP Casino (web) is running - play money only.\n"
                  << "  Open:   http://localhost:" << options.port << "/\n";
        if (options.host != "127.0.0.1") {
            std::cout << "  Friends on the same network can join at http://<this-computer's-IP>:" << options.port << "/\n";
        } else {
            std::cout << "  (Only this computer can connect. Restart with --host 0.0.0.0 to let others join.)\n";
        }
        std::cout << "  Saved data: " << options.dataDir << "/    Press Ctrl+C to stop.\n\n";

        while (!g_stopRequested) std::this_thread::sleep_for(std::chrono::milliseconds(200));

        std::cout << "\nShutting down...\n";
        server.stop();
        session.shutdown();
        core.shutdown();
        for (const std::string& n : core.notices()) std::cout << "  [!] " << n << '\n';
        std::cout << "Saved. Goodbye!\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Web server error: " << e.what() << '\n';
        return 1;
    }
}

}  // namespace casino
