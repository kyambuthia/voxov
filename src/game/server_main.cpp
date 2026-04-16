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

struct ServerTelemetry {
    std::chrono::steady_clock::time_point window_start{};
    uint64_t loop_count = 0;
    std::chrono::microseconds pump_time{0};
    std::chrono::microseconds sleep_time{0};
};
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

    ServerTelemetry telemetry{};
    telemetry.window_start = std::chrono::steady_clock::now();
    constexpr auto kTelemetryInterval = std::chrono::seconds(5);

    while (keep_running.load()) {
        const auto loop_begin = std::chrono::steady_clock::now();
        server.pump();
        const auto after_pump = std::chrono::steady_clock::now();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        const auto loop_end = std::chrono::steady_clock::now();

        telemetry.loop_count += 1;
        telemetry.pump_time +=
            std::chrono::duration_cast<std::chrono::microseconds>(after_pump -
                                                                   loop_begin);
        telemetry.sleep_time +=
            std::chrono::duration_cast<std::chrono::microseconds>(loop_end -
                                                                   after_pump);

        if (loop_end - telemetry.window_start >= kTelemetryInterval) {
            const auto elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    loop_end - telemetry.window_start);
            const double elapsed_seconds =
                static_cast<double>(elapsed.count()) / 1000.0;
            const double loop_hz = elapsed_seconds > 0.0
                                       ? static_cast<double>(telemetry.loop_count) /
                                             elapsed_seconds
                                       : 0.0;
            const double avg_pump_ms =
                telemetry.loop_count > 0
                    ? static_cast<double>(telemetry.pump_time.count()) /
                          static_cast<double>(telemetry.loop_count) / 1000.0
                    : 0.0;
            const double avg_sleep_ms =
                telemetry.loop_count > 0
                    ? static_cast<double>(telemetry.sleep_time.count()) /
                          static_cast<double>(telemetry.loop_count) / 1000.0
                    : 0.0;
            const NetDebugStats stats = server.debug_stats();
            std::fprintf(
                stderr,
                "TEL frame=%.1f/s fixed=snap:%u/s pst:%u/s render=na net=tx:%u/%u rx:%u/%u invalid=%llu chunk=na activity=pump:%.3fms sleep:%.3fms\n",
                loop_hz,
                stats.snapshots_sent_per_sec,
                stats.player_state_broadcasts_per_sec,
                stats.tx_packets_per_sec,
                stats.tx_bytes_per_sec,
                stats.rx_packets_per_sec,
                stats.rx_bytes_per_sec,
                static_cast<unsigned long long>(stats.invalid_packets_total),
                avg_pump_ms,
                avg_sleep_ms);

            telemetry.window_start = loop_end;
            telemetry.loop_count = 0;
            telemetry.pump_time = std::chrono::microseconds{0};
            telemetry.sleep_time = std::chrono::microseconds{0};
        }
    }

    server.shutdown();
    return 0;
}
