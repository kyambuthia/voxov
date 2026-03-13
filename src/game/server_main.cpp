#include "engine_net/net_server.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace {
std::atomic<bool> keep_running{true};

void on_signal(int) { keep_running = false; }
} // namespace

int main(int argc, char **argv) {
    uint16_t port = 7777;
    bool loopback_only = false;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = static_cast<uint16_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (std::strcmp(argv[i], "--loopback-only") == 0 ||
                   std::strcmp(argv[i], "--local-only") == 0) {
            loopback_only = true;
        } else if (std::strcmp(argv[i], "--lan") == 0) {
            loopback_only = false;
        }
    }

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    NetServer server;
    if (!server.init(port, loopback_only)) {
        std::fprintf(stderr, "Failed to start server on port %u\n", port);
        return 1;
    }

    std::fprintf(stderr, "VOXOV server started on port %u (%s)\n",
                 server.bound_port(), loopback_only ? "loopback-only" : "lan");

    while (keep_running.load()) {
        server.pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    server.shutdown();
    return 0;
}
