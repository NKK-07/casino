// WebApp.hpp - starts the HTTP server, the web session and the casino core.
#pragma once

#include <string>

namespace casino {

struct WebOptions {
    std::string host = "127.0.0.1";  // use 0.0.0.0 so other devices on the network can join
    int port = 8080;
    std::string webDir = "web";
    std::string dataDir = "data";
};

// Runs until Ctrl+C. Returns a process exit code.
int runWebServer(const WebOptions& options);

}  // namespace casino
