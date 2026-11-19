// HttpServer.hpp - a minimal HTTP/1.1 server built on plain sockets, enough to
// serve the web UI's static files and a small JSON API. One thread per
// connection, "Connection: close", GET and POST only.
#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>

namespace casino {

struct HttpRequest {
    std::string method;
    std::string path;
    std::map<std::string, std::string> params;  // query string and form body, merged
    std::string body;

    std::string param(const std::string& key, const std::string& fallback = "") const {
        const auto it = params.find(key);
        return it == params.end() ? fallback : it->second;
    }
};

struct HttpResponse {
    int status = 200;
    std::string contentType = "application/json; charset=utf-8";
    std::string body;

    static HttpResponse json(const std::string& body, int status = 200) {
        HttpResponse r;
        r.status = status;
        r.body = body;
        return r;
    }
};

class HttpServer {
public:
    using Handler = std::function<HttpResponse(const HttpRequest&)>;

    HttpServer(std::string host, int port, std::string staticDir);
    ~HttpServer();

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    // Exact-path API route, e.g. "/api/state".
    void route(const std::string& path, Handler handler);

    // Starts listening. Throws FileAccessException-style CasinoException if the
    // port is unavailable. Returns once the socket is ready; connections are
    // served on background threads until stop() is called.
    void start();
    void waitUntilStopped();
    void stop();

    int port() const { return port_; }

private:
    void acceptLoop();
    void handleConnection(long long socketHandle);
    HttpResponse dispatch(const HttpRequest& request);
    HttpResponse serveStatic(const std::string& path) const;

    std::string host_;
    int port_;
    std::string staticDir_;
    std::map<std::string, Handler> routes_;
    std::thread acceptThread_;
    long long listenSocket_ = -1;
    std::atomic<bool> running_{false};
    std::atomic<int> activeConnections_{0};
    std::mutex stopMutex_;
};

}  // namespace casino
