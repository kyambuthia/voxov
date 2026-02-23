#include "engine_net/net_server.hpp"

#include <enet/enet.h>
#include <spdlog/spdlog.h>

#include <cstring>
#include <cstdio>
#include <cmath>
#include <chrono>

namespace {
#pragma pack(push, 1)
struct InputPacket {
    NetPacketHeader header{};
    NetTickInput input{};
};

struct SnapshotPacket {
    NetPacketHeader header{};
    NetSnapshot snapshot{};
};

struct ChunkInterestPacket {
    NetPacketHeader header{};
    NetChunkInterest interest{};
};

struct ChunkStatePacket {
    NetPacketHeader header{};
    NetChunkState state{};
};

struct AssignPlayerPacket {
    NetPacketHeader header{};
    NetAssignPlayer payload{};
};

struct PlayerStatePacket {
    NetPacketHeader header{};
    NetPlayerState state{};
};

struct PlayerRemovePacket {
    NetPacketHeader header{};
    NetPlayerRemove payload{};
};
#pragma pack(pop)

constexpr float kServerTickDt = 1.0f / 60.0f;
constexpr float kServerSpawnY = 6.05f;
constexpr double kServerSimTickMs = 1000.0 / 60.0;
constexpr uint64_t kSnapshotSendIntervalMs = 33;
constexpr uint64_t kPlayerBroadcastIntervalMs = 33;
constexpr int kMaxCatchupTicksPerPump = 8;

float server_anim_cycle_rate(uint8_t anim_state) {
    switch (anim_state) {
    case 1: // walk
        return 5.0f;
    case 2: // run
        return 8.0f;
    case 3: // jump
        return 3.0f;
    case 4: // crawl
        return 2.8f;
    default:
        return 1.0f;
    }
}

float server_anim_blend_target(uint8_t anim_state) {
    switch (anim_state) {
    case 1:
        return 0.5f;
    case 2:
        return 1.0f;
    case 3:
        return 0.75f;
    case 4:
        return 0.35f;
    default:
        return 0.0f;
    }
}

uint64_t now_ms() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}
}

int32_t NetServer::chunk_key(NetChunkCoord coord) const {
    return (static_cast<int32_t>(coord.x) << 16) ^ static_cast<uint16_t>(coord.z);
}

void NetServer::send_chunk_state(ENetPeer *peer, ClientState &state, NetChunkCoord coord, uint32_t version) {
    const int32_t key = chunk_key(coord);
    auto it = state.sent_chunks.find(key);
    if (it != state.sent_chunks.end() && it->second == version) {
        return;
    }

    ChunkStatePacket packet{};
    packet.header = net_make_header(NetMsgType::ChunkState, static_cast<uint8_t>(sizeof(packet.state)));
    packet.state.coord = coord;
    packet.state.version = version;

    ENetPacket *out = enet_packet_create(&packet, sizeof(packet), ENET_PACKET_FLAG_RELIABLE);
    enet_peer_send(peer, static_cast<uint8_t>(NetChannel::Reliable), out);
    record_tx(sizeof(packet));
    state.sent_chunks[key] = version;
}

void NetServer::simulate_client_tick(ClientState &state) {
    float move_x = state.last_input.move_x;
    float move_y = state.last_input.move_y;
    const float len = std::sqrt(move_x * move_x + move_y * move_y);
    if (len > 1.0f) {
        move_x /= len;
        move_y /= len;
    }

    const bool jump_pressed = state.jump_pressed_latched;
    state.jump_pressed_latched = false;
    const bool sprint_held = net_flag_set(state.last_input.action_flags, NetInputFlags::SprintHeld);
    const bool crouch_held = net_flag_set(state.last_input.action_flags, NetInputFlags::CrouchHeld);

    float speed = 4.0f;
    if (crouch_held) {
        speed = 2.2f;
    } else if (sprint_held) {
        speed = 7.2f;
    }

    state.state.x += move_x * speed * kServerTickDt;
    state.state.z += move_y * speed * kServerTickDt;
    state.state.vx = move_x * speed;
    state.state.vz = move_y * speed;

    const bool grounded = state.state.y <= (kServerSpawnY + 0.001f) && std::fabs(state.state.vy) < 0.001f;
    if (jump_pressed && grounded) {
        state.state.vy = 5.5f;
    }
    state.state.vy += -19.62f * kServerTickDt;
    state.state.y += state.state.vy * kServerTickDt;
    if (state.state.y < kServerSpawnY) {
        state.state.y = kServerSpawnY;
        state.state.vy = 0.0f;
    }

    const float planar_speed = std::sqrt(state.state.vx * state.state.vx + state.state.vz * state.state.vz);
    if (state.state.y > (kServerSpawnY + 0.02f) || std::fabs(state.state.vy) > 0.08f) {
        state.state.anim_state = 3;
    } else if (planar_speed > 0.2f) {
        if (crouch_held) {
            state.state.anim_state = 4;
        } else if (sprint_held) {
            state.state.anim_state = 2;
        } else {
            state.state.anim_state = 1;
        }
    } else {
        state.state.anim_state = 0;
    }

    state.state.anim_phase += server_anim_cycle_rate(state.state.anim_state) * kServerTickDt;
    if (state.state.anim_phase > 6.28318530718f) {
        state.state.anim_phase = std::fmod(state.state.anim_phase, 6.28318530718f);
    }
    const float target_blend = server_anim_blend_target(state.state.anim_state);
    state.state.anim_blend += (target_blend - state.state.anim_blend) * 0.18f;
}

void NetServer::simulate_fixed_tick() {
    for (auto &[peer_ptr, state] : clients) {
        (void)peer_ptr;
        simulate_client_tick(state);
    }
}

void NetServer::send_snapshots() {
    if (!server || clients.empty()) {
        return;
    }
    const uint64_t now = now_ms();
    if (last_snapshot_send_ms != 0 && (now - last_snapshot_send_ms) < kSnapshotSendIntervalMs) {
        return;
    }
    last_snapshot_send_ms = now;

    for (auto &[peer_ptr, state] : clients) {
        SnapshotPacket snap{};
        snap.header = net_make_header(NetMsgType::Snapshot, static_cast<uint8_t>(sizeof(snap.snapshot)));
        snap.snapshot.player_id = state.player_id;
        snap.snapshot.tick = state.last_input.tick;
        snap.snapshot.x = state.state.x;
        snap.snapshot.y = state.state.y;
        snap.snapshot.z = state.state.z;
        snap.snapshot.vx = state.state.vx;
        snap.snapshot.vy = state.state.vy;
        snap.snapshot.vz = state.state.vz;
        ENetPacket *out = enet_packet_create(&snap, sizeof(snap), 0);
        enet_peer_send(peer_ptr, static_cast<uint8_t>(NetChannel::Unreliable), out);
        record_tx(sizeof(snap));
        debug_counters.snapshots_sent_window += 1;
    }
}

void NetServer::broadcast_player_states() {
    const uint64_t now = now_ms();
    if (last_player_broadcast_ms != 0 && (now - last_player_broadcast_ms) < kPlayerBroadcastIntervalMs) {
        return;
    }
    last_player_broadcast_ms = now;

    for (auto &[peer_ptr, state] : clients) {
        (void)peer_ptr;
        PlayerStatePacket packet{};
        packet.header = net_make_header(NetMsgType::PlayerState, static_cast<uint8_t>(sizeof(packet.state)));
        packet.state = state.state;
        packet.state.sequence = state.next_player_state_sequence++;

        ENetPacket *out = enet_packet_create(&packet, sizeof(packet), 0);
        enet_host_broadcast(server, static_cast<uint8_t>(NetChannel::Unreliable), out);
        record_tx(sizeof(packet), static_cast<uint32_t>(clients.size()));
        debug_counters.player_state_broadcasts_window += static_cast<uint32_t>(clients.size());
    }
}

void NetServer::broadcast_player_remove(uint32_t player_id) {
    if (!server || player_id == 0) {
        return;
    }
    PlayerRemovePacket packet{};
    packet.header = net_make_header(NetMsgType::PlayerRemove, static_cast<uint8_t>(sizeof(packet.payload)));
    packet.payload.player_id = player_id;
    ENetPacket *out = enet_packet_create(&packet, sizeof(packet), ENET_PACKET_FLAG_RELIABLE);
    enet_host_broadcast(server, static_cast<uint8_t>(NetChannel::Reliable), out);
    record_tx(sizeof(packet), static_cast<uint32_t>(clients.size()));
}

void NetServer::record_tx(size_t bytes, uint32_t packet_count) {
    if (packet_count == 0) {
        return;
    }
    debug_counters.tx_packets_total += packet_count;
    debug_counters.tx_bytes_total += static_cast<uint64_t>(bytes) * static_cast<uint64_t>(packet_count);
    debug_counters.tx_packets_window += packet_count;
    debug_counters.tx_bytes_window += static_cast<uint32_t>(bytes * packet_count);
}

void NetServer::record_rx(size_t bytes) {
    debug_counters.rx_packets_total += 1;
    debug_counters.rx_bytes_total += static_cast<uint64_t>(bytes);
    debug_counters.rx_packets_window += 1;
    debug_counters.rx_bytes_window += static_cast<uint32_t>(bytes);
}

void NetServer::refresh_debug_stats() {
    const uint64_t now = now_ms();
    if (debug_counters.last_rollup_ms == 0) {
        debug_counters.last_rollup_ms = now;
    }
    if ((now - debug_counters.last_rollup_ms) < 1000) {
        return;
    }

    debug_counters.snapshot.tx_packets_total = debug_counters.tx_packets_total;
    debug_counters.snapshot.tx_bytes_total = debug_counters.tx_bytes_total;
    debug_counters.snapshot.rx_packets_total = debug_counters.rx_packets_total;
    debug_counters.snapshot.rx_bytes_total = debug_counters.rx_bytes_total;
    debug_counters.snapshot.invalid_packets_total = debug_counters.invalid_packets_total;
    debug_counters.snapshot.tx_packets_per_sec = debug_counters.tx_packets_window;
    debug_counters.snapshot.tx_bytes_per_sec = debug_counters.tx_bytes_window;
    debug_counters.snapshot.rx_packets_per_sec = debug_counters.rx_packets_window;
    debug_counters.snapshot.rx_bytes_per_sec = debug_counters.rx_bytes_window;
    debug_counters.snapshot.snapshots_sent_per_sec = debug_counters.snapshots_sent_window;
    debug_counters.snapshot.player_state_broadcasts_per_sec = debug_counters.player_state_broadcasts_window;

    debug_counters.tx_packets_window = 0;
    debug_counters.tx_bytes_window = 0;
    debug_counters.rx_packets_window = 0;
    debug_counters.rx_bytes_window = 0;
    debug_counters.snapshots_sent_window = 0;
    debug_counters.player_state_broadcasts_window = 0;
    debug_counters.last_rollup_ms = now;
}

bool NetServer::init(uint16_t port, bool loopback_only) {
    if (initialized) {
        return server != nullptr;
    }

    if (enet_initialize() != 0) {
        std::fprintf(stderr, "NetServer: enet_initialize failed\n");
        return false;
    }
    initialized = true;

    ENetAddress address{};
    local_only = loopback_only;
    if (local_only) {
        if (enet_address_set_host(&address, "127.0.0.1") != 0) {
            std::fprintf(stderr, "NetServer: failed to resolve loopback host\n");
            enet_deinitialize();
            initialized = false;
            return false;
        }
    } else {
        address.host = ENET_HOST_ANY;
    }
    address.port = port;
    server = enet_host_create(&address, 32, 2, 0, 0);
    if (!server) {
        std::fprintf(stderr, "NetServer: enet_host_create failed on port %u\n", port);
        enet_deinitialize();
        initialized = false;
        return false;
    }
    last_player_broadcast_ms = 0;
    last_pump_ms = 0;
    sim_accumulator_ms = 0.0;
    last_snapshot_send_ms = 0;
    debug_counters = DebugCounters{};
    refresh_debug_stats();
    return true;
}

void NetServer::shutdown() {
    clients.clear();
    last_pump_ms = 0;
    sim_accumulator_ms = 0.0;
    last_snapshot_send_ms = 0;
    last_player_broadcast_ms = 0;
    if (server) {
        enet_host_destroy(server);
        server = nullptr;
    }
    if (initialized) {
        enet_deinitialize();
        initialized = false;
    }
    debug_counters = DebugCounters{};
}

void NetServer::pump() {
    if (!server) {
        return;
    }
    refresh_debug_stats();

    const uint64_t pump_now = now_ms();
    if (last_pump_ms == 0) {
        last_pump_ms = pump_now;
    }
    uint64_t delta_ms = pump_now - last_pump_ms;
    last_pump_ms = pump_now;
    if (delta_ms > 250) {
        delta_ms = 250;
    }
    sim_accumulator_ms += static_cast<double>(delta_ms);

    ENetEvent event{};
    while (enet_host_service(server, &event, 0) > 0) {
        switch (event.type) {
        case ENET_EVENT_TYPE_CONNECT: {
            ClientState state{};
            state.player_id = next_player_id++;
            state.state.player_id = state.player_id;
            state.state.x = 8.0f + static_cast<float>((state.player_id % 3) * 2);
            state.state.y = kServerSpawnY;
            state.state.z = 8.0f;
            state.state.anim_state = 0;
            if (local_only) {
                char ip_buffer[64]{};
                if (enet_address_get_host_ip(&event.peer->address, ip_buffer, sizeof(ip_buffer)) != 0) {
                    std::snprintf(ip_buffer, sizeof(ip_buffer), "%s", "unknown");
                }
                const bool localhost = std::strcmp(ip_buffer, "127.0.0.1") == 0 || std::strcmp(ip_buffer, "::1") == 0;
                if (!localhost) {
                    spdlog::warn("NetServer: rejected non-local peer {} in loopback-only mode", ip_buffer);
                    enet_peer_disconnect_now(event.peer, 0);
                    break;
                }
            }
            clients[event.peer] = state;
            spdlog::info("NetServer: client connected, assigned player_id={}, clients={}", state.player_id, clients.size());

            AssignPlayerPacket assign{};
            assign.header = net_make_header(NetMsgType::AssignPlayer, static_cast<uint8_t>(sizeof(assign.payload)));
            assign.payload.player_id = state.player_id;
            ENetPacket *out = enet_packet_create(&assign, sizeof(assign), ENET_PACKET_FLAG_RELIABLE);
            enet_peer_send(event.peer, static_cast<uint8_t>(NetChannel::Reliable), out);
            record_tx(sizeof(assign));
            break;
        }
        case ENET_EVENT_TYPE_DISCONNECT:
        {
            const auto it = clients.find(event.peer);
            if (it != clients.end()) {
                const uint32_t removed_player_id = it->second.player_id;
                clients.erase(it);
                broadcast_player_remove(removed_player_id);
            }
            spdlog::info("NetServer: client disconnected, clients={}", clients.size());
            break;
        }
        case ENET_EVENT_TYPE_RECEIVE: {
            record_rx(event.packet->dataLength);
            bool recognized_message = false;
            auto it = clients.find(event.peer);
            if (it == clients.end()) {
                debug_counters.invalid_packets_total += 1;
                enet_packet_destroy(event.packet);
                break;
            }

            ClientState &state = it->second;

            if (event.packet->dataLength >= sizeof(NetPacketHeader)) {
                NetPacketHeader header{};
                std::memcpy(&header, event.packet->data, sizeof(header));
                if (net_header_basic_valid(header) &&
                    event.packet->dataLength == (sizeof(NetPacketHeader) + header.payload_size)) {
                    switch (static_cast<NetMsgType>(header.type)) {
                    case NetMsgType::Input:
                        if (header.payload_size == sizeof(NetTickInput) &&
                            event.packet->dataLength == sizeof(InputPacket)) {
                            InputPacket packet{};
                            std::memcpy(&packet, event.packet->data, sizeof(packet));
                            recognized_message = true;
                            state.last_input = packet.input;
                            if (net_flag_set(packet.input.action_flags, NetInputFlags::JumpPressed)) {
                                state.jump_pressed_latched = true;
                            }
                        }
                        break;
                    case NetMsgType::ChunkInterest:
                        if (header.payload_size == sizeof(NetChunkInterest) &&
                            event.packet->dataLength == sizeof(ChunkInterestPacket)) {
                            ChunkInterestPacket interest_packet{};
                            std::memcpy(&interest_packet, event.packet->data, sizeof(interest_packet));
                            recognized_message = true;
                            state.interest = interest_packet.interest;
                            for (int dz = -state.interest.radius; dz <= state.interest.radius; ++dz) {
                                for (int dx = -state.interest.radius; dx <= state.interest.radius; ++dx) {
                                    NetChunkCoord coord{};
                                    coord.x = static_cast<int16_t>(state.interest.center_x + dx);
                                    coord.z = static_cast<int16_t>(state.interest.center_z + dz);
                                    send_chunk_state(event.peer, state, coord, 1);
                                }
                            }
                        }
                        break;
                    default:
                        break;
                    }
                }
            }
            if (!recognized_message) {
                debug_counters.invalid_packets_total += 1;
            }

            enet_packet_destroy(event.packet);
            break;
        }
        default:
            break;
        }
    }

    int catchup_ticks = 0;
    while (sim_accumulator_ms >= kServerSimTickMs && catchup_ticks < kMaxCatchupTicksPerPump) {
        simulate_fixed_tick();
        sim_accumulator_ms -= kServerSimTickMs;
        ++catchup_ticks;
    }
    if (catchup_ticks == kMaxCatchupTicksPerPump && sim_accumulator_ms >= kServerSimTickMs) {
        sim_accumulator_ms = std::fmod(sim_accumulator_ms, kServerSimTickMs);
        spdlog::warn("NetServer: sim catch-up capped ({} ticks), dropping accumulated time", kMaxCatchupTicksPerPump);
    }

    send_snapshots();
    broadcast_player_states();
}

NetDebugStats NetServer::debug_stats() const {
    return debug_counters.snapshot;
}
