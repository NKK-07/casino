#include "HttpServer.hpp"

#include <cctype>
#include <chrono>
#include <cstring>
#include <fstream>
#include <sstream>

#include "Exceptions.hpp"

#if defined(_WIN32)
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using SocketT = SOCKET;
static const SocketT kBadSocket = INVALID_SOCKET;
static void closeSocket(SocketT s) { closesocket(s); }
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
using SocketT = int;
static const SocketT kBadSocket = -1;
static void closeSocket(SocketT s) { close(s); }
#endif

namespace casino {
namespace {

#if defined(_WIN32)
struct WinsockRuntime {
    WinsockRuntime() {
        WSADATA data;
        WSAStartup(MAKEWORD(2, 2), &data);
    }
    ~WinsockRuntime() { WSACleanup(); }
};
#endif

void setReceiveTimeout(SocketT s, int seconds) {
#if defined(_WIN32)
    const DWORD ms = static_cast<DWORD>(seconds) * 1000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&ms), sizeof ms);
#else
    timeval tv{};
    tv.tv_sec = seconds;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
#endif
}

std::string urlDecode(const std::string& in) {
    std::string out;
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '+') {
            out.push_back(' ');
        } else if (in[i] == '%' && i + 2 < in.size() + 0 && std::isxdigit(static_cast<unsigned char>(in[i + 1])) &&
                   std::isxdigit(static_cast<unsigned char>(in[i + 2]))) {
            out.push_back(static_cast<char>(std::stoi(in.substr(i + 1, 2), nullptr, 16)));
            i += 2;
        } else {
            out.push_back(in[i]);
        }
    }
    return out;
}

void parseParams(const std::string& text, std::map<std::string, std::string>& into) {
    std::size_t pos = 0;
    while (pos <= text.size()) {
        std::size_t amp = text.find('&', pos);
        if (amp == std::string::npos) amp = text.size();
        const std::string pair = text.substr(pos, amp - pos);
        if (!pair.empty()) {
            const std::size_t eq = pair.find('=');
            if (eq == std::string::npos) into[urlDecode(pair)] = "";
            else into[urlDecode(pair.substr(0, eq))] = urlDecode(pair.substr(eq + 1));
        }
        pos = amp + 1;
    }
}

const char* statusText(int status) {
    switch (status) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 431: return "Request Header Fields Too Large";
        default: return "Internal Server Error";
    }
}

std::string mimeFor(const std::string& path) {
    const std::size_t dot = path.rfind('.');
    const std::string ext = dot == std::string::npos ? "" : path.substr(dot + 1);
    if (ext == "html") return "text/html; charset=utf-8";
    if (ext == "js") return "application/javascript; charset=utf-8";
    if (ext == "css") return "text/css; charset=utf-8";
    if (ext == "json") return "application/json; charset=utf-8";
    if (ext == "svg") return "image/svg+xml";
    if (ext == "png") return "image/png";
    if (ext == "ico") return "image/x-icon";
    if (ext == "txt") return "text/plain; charset=utf-8";
    return "application/octet-stream";
}

void sendAll(SocketT s, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const int n = static_cast<int>(send(s, data.data() + sent, static_cast<int>(data.size() - sent), 0));
        if (n <= 0) return;
        sent += static_cast<std::size_t>(n);
    }
}

}  // namespace

HttpServer::HttpServer(std::string host, int port, std::string staticDir)
    : host_(std::move(host)), port_(port), staticDir_(std::move(staticDir)) {}

HttpServer::~HttpServer() { stop(); }

void HttpServer::route(const std::string& path, Handler handler) { routes_[path] = std::move(handler); }

void HttpServer::start() {
#if defined(_WIN32)
    static WinsockRuntime runtime;  // initialised once, torn down at exit
#endif
    SocketT s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == kBadSocket) throw FileAccessException("socket", "cannot create a network socket");

    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof yes);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<unsigned short>(port_));
    if (inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) != 1) {
        closeSocket(s);
        throw CasinoException("Invalid listen address '" + host_ + "' (use something like 127.0.0.1 or 0.0.0.0)");
    }
    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) {
        closeSocket(s);
        throw CasinoException("Cannot listen on " + host_ + ":" + std::to_string(port_) +
                              " - is another program using that port?");
    }
    if (listen(s, 32) != 0) {
        closeSocket(s);
        throw CasinoException("listen() failed");
    }

    listenSocket_ = static_cast<long long>(s);
    running_ = true;
    acceptThread_ = std::thread(&HttpServer::acceptLoop, this);
}

void HttpServer::acceptLoop() {
    while (running_) {
        const SocketT client = accept(static_cast<SocketT>(listenSocket_), nullptr, nullptr);
        if (client == kBadSocket) {
            if (!running_) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }
        ++activeConnections_;
        std::thread([this, client] { handleConnection(static_cast<long long>(client)); }).detach();
    }
}

void HttpServer::waitUntilStopped() {
    if (acceptThread_.joinable() && std::this_thread::get_id() != acceptThread_.get_id()) acceptThread_.join();
}

void HttpServer::stop() {
    std::lock_guard<std::mutex> guard(stopMutex_);
    if (running_.exchange(false)) {
        if (listenSocket_ >= 0) closeSocket(static_cast<SocketT>(listenSocket_));
        listenSocket_ = -1;
    }
    if (acceptThread_.joinable() && std::this_thread::get_id() != acceptThread_.get_id()) acceptThread_.join();
    for (int i = 0; i < 100 && activeConnections_ > 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(20));
}

void HttpServer::handleConnection(long long handle) {
    const SocketT sock = static_cast<SocketT>(handle);
    try {
        setReceiveTimeout(sock, 10);

        std::string data;
        char buffer[4096];
        std::size_t headerEnd = std::string::npos;
        while ((headerEnd = data.find("\r\n\r\n")) == std::string::npos) {
            const int n = static_cast<int>(recv(sock, buffer, sizeof buffer, 0));
            if (n <= 0) throw CasinoException("client closed");
            data.append(buffer, static_cast<std::size_t>(n));
            if (data.size() > 32768) throw CasinoException("header too large");
        }

        std::istringstream head(data.substr(0, headerEnd));
        HttpRequest request;
        std::string target, version, line;
        head >> request.method >> target >> version;
        std::getline(head, line);  // rest of the request line

        std::size_t contentLength = 0;
        while (std::getline(head, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const std::size_t colon = line.find(':');
            if (colon == std::string::npos) continue;
            std::string key = line.substr(0, colon);
            for (char& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (key == "content-length") contentLength = static_cast<std::size_t>(std::atol(line.c_str() + colon + 1));
        }
        if (contentLength > 65536) contentLength = 65536;

        request.body = data.substr(headerEnd + 4);
        while (request.body.size() < contentLength) {
            const int n = static_cast<int>(recv(sock, buffer, sizeof buffer, 0));
            if (n <= 0) break;
            request.body.append(buffer, static_cast<std::size_t>(n));
        }

        const std::size_t q = target.find('?');
        request.path = urlDecode(q == std::string::npos ? target : target.substr(0, q));
        if (q != std::string::npos) parseParams(target.substr(q + 1), request.params);
        if (request.method == "POST") parseParams(request.body, request.params);

        const HttpResponse response = dispatch(request);
        std::ostringstream out;
        out << "HTTP/1.1 " << response.status << ' ' << statusText(response.status) << "\r\n"
            << "Content-Type: " << response.contentType << "\r\n"
            << "Content-Length: " << response.body.size() << "\r\n"
            << "Cache-Control: no-store\r\n"
            << "X-Content-Type-Options: nosniff\r\n"
            << "Connection: close\r\n\r\n";
        sendAll(sock, out.str() + response.body);
    } catch (...) {
        // a broken or malicious client only loses its own connection
    }
    closeSocket(sock);
    --activeConnections_;
}

HttpResponse HttpServer::dispatch(const HttpRequest& request) {
    if (request.method != "GET" && request.method != "POST") {
        return HttpResponse::json("{\"ok\":false,\"error\":\"method not allowed\"}", 405);
    }
    if (request.path.rfind("/api/", 0) == 0) {
        const auto it = routes_.find(request.path);
        if (it == routes_.end()) return HttpResponse::json("{\"ok\":false,\"error\":\"unknown endpoint\"}", 404);
        try {
            return it->second(request);
        } catch (const std::exception& e) {
            std::string message = e.what();
            for (char& c : message) {
                if (c == '"' || c == '\\' || c == '\n' || c == '\r') c = ' ';
            }
            return HttpResponse::json("{\"ok\":false,\"error\":\"" + message + "\"}", 500);
        }
    }
    if (request.method != "GET") return HttpResponse::json("{\"ok\":false,\"error\":\"method not allowed\"}", 405);
    return serveStatic(request.path);
}

HttpResponse HttpServer::serveStatic(const std::string& requestPath) const {
    std::string path = requestPath == "/" ? "/index.html" : requestPath;
    if (path.empty() || path[0] != '/' || path.find("..") != std::string::npos ||
        path.find('\\') != std::string::npos || path.find(':') != std::string::npos) {
        return HttpResponse::json("{\"ok\":false,\"error\":\"bad path\"}", 400);
    }
    std::ifstream in(staticDir_ + path, std::ios::binary);
    if (!in) {
        HttpResponse r;
        r.status = 404;
        r.contentType = "text/plain; charset=utf-8";
        r.body = "404 - not found";
        return r;
    }
    std::ostringstream content;
    content << in.rdbuf();
    HttpResponse r;
    r.contentType = mimeFor(path);
    r.body = content.str();
    return r;
}

}  // namespace casino
