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
    uint32_t snapshots_after_reconnect = 0;
    uint32_t chunk_updates = 0;
    uint32_t remote_state_frames = 0;
    uint32_t max_remote_seen = 0;
    bool spherical_state_seen = false;
    uint32_t forced_disconnects = 0;
    bool reconnect_attempted = false;
    bool assigned_after_reconnect = false;
};

struct StressScenario {
    const char *name = "";
    int client_count = 0;
    int ticks = 0;
    int sleep_ms = 0;
    bool reconnect_cycle = false;
};

uint16_t acquire_loopback_port(NetServer &server) {
    constexpr uint16_t kPortBase = 20000;
    constexpr uint16_t kPortSpan = 20000;
    constexpr uint16_t kPortAttempts = 64;

    const uint64_t now_ticks = static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const uint16_t start_offset = static_cast<uint16_t>(now_ticks % kPortSpan);

    for (uint16_t attempt = 0; attempt < kPortAttempts; ++attempt) {
        const uint16_t offset = static_cast<uint16_t>((start_offset + attempt) % kPortSpan);
        const uint16_t candidate = static_cast<uint16_t>(kPortBase + offset);
        if (server.init(candidate, true)) {
            return candidate;
        }
    }
    return 0;
}

bool run_scenario(const StressScenario &scenario) {
    if (scenario.client_count <= 0 || scenario.ticks <= 0 || scenario.sleep_ms < 0) {
        std::fprintf(stderr, "FAIL[%s]: invalid scenario config\n", scenario.name);
        return false;
    }

    NetServer server;
    const uint16_t port = acquire_loopback_port(server);
    if (port == 0) {
        std::fprintf(stderr, "FAIL[%s]: server init failed across loopback test ports\n", scenario.name);
        return false;
    }

    std::vector<NetClient> clients(static_cast<size_t>(scenario.client_count));
    std::vector<ClientStats> stats(static_cast<size_t>(scenario.client_count));
    std::vector<bool> reconnect_target(static_cast<size_t>(scenario.client_count), false);
    std::vector<bool> temporarily_disconnected(static_cast<size_t>(scenario.client_count), false);

    NetChunkInterest interest{};
    interest.center_x = 0;
    interest.center_z = 0;
    interest.radius = 2;

    auto cleanup = [&]() {
        for (NetClient &client : clients) {
            client.disconnect();
            client.shutdown();
        }
        server.shutdown();
    };

    for (int i = 0; i < scenario.client_count; ++i) {
        if (!clients[static_cast<size_t>(i)].init()) {
            std::fprintf(stderr, "FAIL[%s]: client[%d] init failed\n", scenario.name, i);
            cleanup();
            return false;
        }
        if (!clients[static_cast<size_t>(i)].connect("127.0.0.1", port)) {
            std::fprintf(stderr, "FAIL[%s]: client[%d] connect enqueue failed on port %u\n", scenario.name, i, port);
            cleanup();
            return false;
        }
        clients[static_cast<size_t>(i)].set_chunk_interest(interest);
    }

    int disconnect_tick = -1;
    int reconnect_tick = -1;
    if (scenario.reconnect_cycle) {
        disconnect_tick = scenario.ticks / 3;
        reconnect_tick = disconnect_tick + std::max(30, scenario.ticks / 10);
        bool any_target = false;
        for (int i = 0; i < scenario.client_count; ++i) {
            if ((i % 5) == 2) {
                reconnect_target[static_cast<size_t>(i)] = true;
                any_target = true;
            }
        }
        if (!any_target) {
            reconnect_target[0] = true;
        }
    }

    bool ok = true;
    for (int tick = 0; tick < scenario.ticks; ++tick) {
        server.pump();

        if (scenario.reconnect_cycle && tick == disconnect_tick) {
            for (int i = 0; i < scenario.client_count; ++i) {
                if (!reconnect_target[static_cast<size_t>(i)]) {
                    continue;
                }
                clients[static_cast<size_t>(i)].disconnect();
                temporarily_disconnected[static_cast<size_t>(i)] = true;
                stats[static_cast<size_t>(i)].forced_disconnects += 1;
            }
        }

        if (scenario.reconnect_cycle && tick == reconnect_tick) {
            for (int i = 0; i < scenario.client_count; ++i) {
                if (!temporarily_disconnected[static_cast<size_t>(i)]) {
                    continue;
                }
                if (!clients[static_cast<size_t>(i)].connect("127.0.0.1", port)) {
                    std::fprintf(stderr, "FAIL[%s]: client[%d] reconnect enqueue failed on port %u\n", scenario.name, i, port);
                    ok = false;
                    break;
                }
                clients[static_cast<size_t>(i)].set_chunk_interest(interest);
                temporarily_disconnected[static_cast<size_t>(i)] = false;
                stats[static_cast<size_t>(i)].reconnect_attempted = true;
            }
            if (!ok) {
                break;
            }
        }

        for (int i = 0; i < scenario.client_count; ++i) {
            NetClient &client = clients[static_cast<size_t>(i)];
            ClientStats &s = stats[static_cast<size_t>(i)];

            client.pump();
            s.connected = client.is_connected();

            const uint32_t local_id = client.local_player_id();
            if (local_id != 0) {
                s.assigned = true;
                s.assigned_id = local_id;
                if (s.reconnect_attempted && tick >= reconnect_tick) {
                    s.assigned_after_reconnect = true;
                }
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

                if ((tick & 1) == 0) {
                    NetPlayerState player_state{};
                    player_state.player_id = local_id;
                    player_state.tick = static_cast<uint32_t>(tick);
                    player_state.sequence = static_cast<uint32_t>(tick);
                    player_state.x = 2'000'000.0f + static_cast<float>(i * 3);
                    player_state.y = static_cast<float>(i);
                    player_state.z = static_cast<float>(tick % 120) * 0.05f;
                    player_state.vz = 3.0f;
                    client.send_player_state(player_state);
                }
            }

            NetSnapshot snapshot{};
            while (client.poll_snapshot(snapshot)) {
                if (std::isfinite(snapshot.x) && std::isfinite(snapshot.y) && std::isfinite(snapshot.z)) {
                    s.snapshots++;
                    if (s.reconnect_attempted && tick >= reconnect_tick) {
                        s.snapshots_after_reconnect++;
                    }
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
            for (const auto &[pid, state] : players) {
                if (pid != local_id) {
                    remote_count++;
                    if (std::abs(state.x) > 1'000'000.0f &&
                        std::isfinite(state.y) && std::isfinite(state.z)) {
                        s.spherical_state_seen = true;
                    }
                }
            }
            if (remote_count > s.max_remote_seen) {
                s.max_remote_seen = remote_count;
            }
        }

        if (scenario.sleep_ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(scenario.sleep_ms));
        }
    }

    int assigned_count = 0;
    int snapshot_clients = 0;
    int chunk_clients = 0;
    int remote_visible_clients = 0;
    int spherical_state_clients = 0;
    int reconnect_targets = 0;
    int reconnect_assigned = 0;
    int reconnect_snapshot_ok = 0;

    const uint32_t min_snapshot_threshold = static_cast<uint32_t>(std::max(12, scenario.ticks / 24));

    for (int i = 0; i < scenario.client_count; ++i) {
        const ClientStats &s = stats[static_cast<size_t>(i)];
        if (s.assigned) {
            assigned_count++;
        }
        if (s.snapshots >= min_snapshot_threshold) {
            snapshot_clients++;
        }
        if (s.chunk_updates > 0) {
            chunk_clients++;
        }
        if (s.max_remote_seen > 0) {
            remote_visible_clients++;
        }
        if (s.spherical_state_seen) {
            spherical_state_clients++;
        }
        if (reconnect_target[static_cast<size_t>(i)]) {
            reconnect_targets++;
            if (s.assigned_after_reconnect) {
                reconnect_assigned++;
            }
            if (s.snapshots_after_reconnect >= 6) {
                reconnect_snapshot_ok++;
            }
        }

        std::fprintf(
            stderr,
            "[%s] client[%02d] connected=%d assigned=%u snapshots=%u chunks=%u remote_frames=%u max_remote=%u disc=%u rec_assigned=%d rec_snaps=%u\n",
            scenario.name,
            i,
            s.connected ? 1 : 0,
            s.assigned_id,
            s.snapshots,
            s.chunk_updates,
            s.remote_state_frames,
            s.max_remote_seen,
            s.forced_disconnects,
            s.assigned_after_reconnect ? 1 : 0,
            s.snapshots_after_reconnect);
    }

    if (assigned_count < scenario.client_count) {
        std::fprintf(stderr, "FAIL[%s]: only %d/%d clients received AssignPlayer\n", scenario.name, assigned_count, scenario.client_count);
        ok = false;
    }
    if (snapshot_clients < std::max(1, scenario.client_count - 1)) {
        std::fprintf(stderr, "FAIL[%s]: only %d/%d clients received enough snapshots\n", scenario.name, snapshot_clients, scenario.client_count);
        ok = false;
    }
    if (chunk_clients < std::max(1, scenario.client_count - 1)) {
        std::fprintf(stderr, "FAIL[%s]: only %d/%d clients received chunk updates\n", scenario.name, chunk_clients, scenario.client_count);
        ok = false;
    }
    if (remote_visible_clients < std::max(1, scenario.client_count - 1)) {
        std::fprintf(stderr, "FAIL[%s]: only %d/%d clients observed remote replication\n", scenario.name, remote_visible_clients, scenario.client_count);
        ok = false;
    }
    if (spherical_state_clients < std::max(1, scenario.client_count - 1)) {
        std::fprintf(stderr,
                     "FAIL[%s]: only %d/%d clients observed spherical client-state relay\n",
                     scenario.name, spherical_state_clients,
                     scenario.client_count);
        ok = false;
    }
    if (scenario.reconnect_cycle) {
        if (reconnect_assigned < reconnect_targets) {
            std::fprintf(stderr, "FAIL[%s]: only %d/%d reconnect targets reassigned\n", scenario.name, reconnect_assigned, reconnect_targets);
            ok = false;
        }
        if (reconnect_snapshot_ok < reconnect_targets) {
            std::fprintf(stderr, "FAIL[%s]: only %d/%d reconnect targets received post-reconnect snapshots\n", scenario.name, reconnect_snapshot_ok, reconnect_targets);
            ok = false;
        }
    }

    cleanup();
    if (ok) {
        std::fprintf(
            stderr,
            "PASS[%s]: clients=%d assigned=%d snapshot_ok=%d chunk_ok=%d remote_ok=%d reconnect=%d/%d\n",
            scenario.name,
            scenario.client_count,
            assigned_count,
            snapshot_clients,
            chunk_clients,
            remote_visible_clients,
            reconnect_assigned,
            reconnect_targets);
    }
    return ok;
}
}

int main() {
    const StressScenario scenarios[] = {
        {"s2", 2, 600, 6, false},
        {"s8", 8, 700, 6, false},
        {"s16", 16, 800, 6, true},
        {"s32", 32, 900, 6, true},
    };

    bool all_ok = true;
    for (const StressScenario &scenario : scenarios) {
        if (!run_scenario(scenario)) {
            all_ok = false;
        }
    }

    if (!all_ok) {
        return 1;
    }
    std::fprintf(stderr, "Net stress PASS\n");
    return 0;
}
