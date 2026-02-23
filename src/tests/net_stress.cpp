#include "engine_net/net_client.hpp"
#include "engine_net/net_server.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

namespace {
struct ClientStats {
    bool connected = false;
    bool assigned = false;
    uint32_t assigned_id = 0;
    uint32_t snapshots = 0;
    uint32_t chunk_updates = 0;
    uint32_t remote_state_frames = 0;
    uint32_t max_remote_seen = 0;
};

bool run_stress() {
    constexpr uint16_t kPort = 17777;
    constexpr int kClientCount = 12;
    constexpr int kTicks = 1200; // ~12s at 10ms tick
    constexpr int kSleepMs = 10;

    NetServer server;
    if (!server.init(kPort, true)) {
        std::fprintf(stderr, "FAIL: server init failed on port %u\n", kPort);
        return false;
    }

    std::vector<NetClient> clients(static_cast<size_t>(kClientCount));
    std::vector<ClientStats> stats(static_cast<size_t>(kClientCount));

    NetChunkInterest interest{};
    interest.center_x = 0;
    interest.center_z = 0;
    interest.radius = 2;

    for (int i = 0; i < kClientCount; ++i) {
        if (!clients[static_cast<size_t>(i)].init()) {
            std::fprintf(stderr, "FAIL: client[%d] init failed\n", i);
            return false;
        }
        if (!clients[static_cast<size_t>(i)].connect("127.0.0.1", kPort)) {
            std::fprintf(stderr, "FAIL: client[%d] connect enqueue failed\n", i);
            return false;
        }
        clients[static_cast<size_t>(i)].set_chunk_interest(interest);
    }

    for (int tick = 0; tick < kTicks; ++tick) {
        server.pump();

        for (int i = 0; i < kClientCount; ++i) {
            NetClient &client = clients[static_cast<size_t>(i)];
            ClientStats &s = stats[static_cast<size_t>(i)];

            client.pump();
            s.connected = client.is_connected();

            const uint32_t local_id = client.local_player_id();
            if (local_id != 0) {
                s.assigned = true;
                s.assigned_id = local_id;
            }

            if (client.is_connected()) {
                NetTickInput input{};
                input.tick = static_cast<uint32_t>(tick);
                input.move_x = ((tick + i) % 3 == 0) ? 1.0f : (((tick + i) % 3 == 1) ? -1.0f : 0.0f);
                input.move_y = ((tick / 7 + i) % 3 == 0) ? 1.0f : (((tick / 7 + i) % 3 == 1) ? -1.0f : 0.0f);
                if ((tick + i) % 97 == 0) {
                    input.action_flags |= net_flag(NetInputFlags::JumpPressed);
                }
                if ((tick + i) % 11 < 5) {
                    input.action_flags |= net_flag(NetInputFlags::SprintHeld);
                }
                client.send_input(input);
            }

            NetSnapshot snapshot{};
            while (client.poll_snapshot(snapshot)) {
                if (std::isfinite(snapshot.x) && std::isfinite(snapshot.y) && std::isfinite(snapshot.z)) {
                    s.snapshots++;
                }
            }

            NetChunkState chunk_state{};
            while (client.poll_chunk_state(chunk_state)) {
                (void)chunk_state;
                s.chunk_updates++;
            }

            const auto &players = client.player_states();
            if (!players.empty()) {
                s.remote_state_frames++;
            }
            uint32_t remote_count = 0;
            for (const auto &[pid, _] : players) {
                (void)_;
                if (pid != local_id) {
                    remote_count++;
                }
            }
            if (remote_count > s.max_remote_seen) {
                s.max_remote_seen = remote_count;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(kSleepMs));
    }

    bool ok = true;
    int assigned_count = 0;
    int snapshot_clients = 0;
    int chunk_clients = 0;
    int remote_visible_clients = 0;

    for (int i = 0; i < kClientCount; ++i) {
        const ClientStats &s = stats[static_cast<size_t>(i)];
        if (s.assigned) {
            assigned_count++;
        }
        if (s.snapshots > 50) {
            snapshot_clients++;
        }
        if (s.chunk_updates > 0) {
            chunk_clients++;
        }
        if (s.max_remote_seen > 0) {
            remote_visible_clients++;
        }

        std::fprintf(
            stderr,
            "client[%02d] connected=%d assigned=%u snapshots=%u chunks=%u remote_frames=%u max_remote=%u\n",
            i,
            s.connected ? 1 : 0,
            s.assigned_id,
            s.snapshots,
            s.chunk_updates,
            s.remote_state_frames,
            s.max_remote_seen);
    }

    // Expectations are intentionally conservative to avoid flakiness in CI/local VMs.
    if (assigned_count < kClientCount) {
        std::fprintf(stderr, "FAIL: only %d/%d clients received AssignPlayer\n", assigned_count, kClientCount);
        ok = false;
    }
    if (snapshot_clients < (kClientCount - 1)) {
        std::fprintf(stderr, "FAIL: only %d/%d clients received enough snapshots\n", snapshot_clients, kClientCount);
        ok = false;
    }
    if (chunk_clients < (kClientCount - 1)) {
        std::fprintf(stderr, "FAIL: only %d/%d clients received chunk updates\n", chunk_clients, kClientCount);
        ok = false;
    }
    if (remote_visible_clients < (kClientCount - 2)) {
        std::fprintf(stderr, "FAIL: only %d/%d clients observed remote player replication\n", remote_visible_clients, kClientCount);
        ok = false;
    }

    for (NetClient &client : clients) {
        client.disconnect();
        client.shutdown();
    }
    server.shutdown();

    return ok;
}
}

int main() {
    const bool ok = run_stress();
    if (!ok) {
        return 1;
    }
    std::fprintf(stderr, "Net stress PASS\n");
    return 0;
}
