#include "engine_net/net_server.hpp"
#include "engine_gameplay/animation/player_animation_graph.hpp"

#include <enet/enet.h>
#include <spdlog/spdlog.h>

#include <cstring>
#include <cstdio>
#include <cmath>
#include <chrono>
#include <algorithm>

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

struct ProtocolInfoPacket {
    NetPacketHeader header{};
    NetProtocolInfo payload{};
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
    return player_animation_definition(static_cast<PlayerAnimState>(anim_state)).phase_rate;
}

float server_anim_blend_target(uint8_t anim_state) {
    return player_animation_definition(static_cast<PlayerAnimState>(anim_state)).target_blend;
}

uint64_t now_ms() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}
}

int32_t NetServer::chunk_key(NetChunkCoord coord) const {
    return (static_cast<int32_t>(coord.x) << 16) ^ static_cast<uint16_t>(coord.z);
}

bool NetServer::should_replicate_player_state(const ClientState &observer, const ClientState &subject) const {
    if (observer.player_id == 0 || subject.player_id == 0) {
        return false;
    }
    if (observer.player_id == subject.player_id) {
        return false;
    }

    // Keep replication aligned with chunk-interest requests so distant players
    // don't consume bandwidth in large sessions.
    constexpr float k_chunk_world_size = 16.0f;
    const int32_t subject_chunk_x = static_cast<int32_t>(std::floor(subject.state.x / k_chunk_world_size));
    const int32_t subject_chunk_z = static_cast<int32_t>(std::floor(subject.state.z / k_chunk_world_size));
    const int32_t dx_chunks = std::abs(subject_chunk_x - observer.interest.center_x);
    const int32_t dz_chunks = std::abs(subject_chunk_z - observer.interest.center_z);
    const int32_t allowed_chunk_delta = static_cast<int32_t>(observer.interest.radius) + 1;
    if (dx_chunks <= allowed_chunk_delta && dz_chunks <= allowed_chunk_delta) {
        return true;
    }

    const float dx = subject.state.x - observer.state.x;
    const float dz = subject.state.z - observer.state.z;
    const float max_distance = std::max(
        48.0f,
        (static_cast<float>(observer.interest.radius) + 1.0f) * k_chunk_world_size * 1.5f);
    return ((dx * dx) + (dz * dz)) <= (max_distance * max_distance);
}

void NetServer::send_chunk_state(ENetPeer *peer, ClientState &state, NetChunkCoord coord, uint32_t version) {
    const int32_t key = chunk_key(coord);
    auto it = state.sent_chunks.find(key);
    if (it != state.sent_chunks.end() && it->second == version) {
        return;
    }

    ChunkStatePacket packet{};
    packet.header = net_make_header(
        NetMsgType::ChunkState,
        static_cast<uint16_t>(sizeof(packet.state)),
        next_packet_sequence++);
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
    if (state.state.vy > 0.12f) {
        state.state.anim_state = static_cast<uint8_t>(PlayerAnimState::JumpLoop);
    } else if (state.state.y > (kServerSpawnY + 0.02f) || state.state.vy < -0.12f) {
        state.state.anim_state = static_cast<uint8_t>(PlayerAnimState::FallLoop);
    } else if (planar_speed > 0.2f) {
        state.state.anim_state = static_cast<uint8_t>(
            sprint_held ? PlayerAnimState::LocomotionRun : PlayerAnimState::LocomotionWalk);
    } else {
        state.state.anim_state = static_cast<uint8_t>(PlayerAnimState::Idle);
    }

    state.state.anim_phase += server_anim_cycle_rate(state.state.anim_state) * kServerTickDt;
    if (state.state.anim_phase > 6.28318530718f) {
        state.state.anim_phase = std::fmod(state.state.anim_phase, 6.28318530718f);
    }
    const float target_blend = server_anim_blend_target(state.state.anim_state);
    state.state.anim_blend += (target_blend - state.state.anim_blend) * 0.18f;
    state.state.tick = server_sim_tick;
}

void NetServer::simulate_fixed_tick() {
    server_sim_tick += 1;
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
        snap.header = net_make_header(
            NetMsgType::Snapshot,
            static_cast<uint16_t>(sizeof(snap.snapshot)),
            next_packet_sequence++);
        snap.snapshot.player_id = state.player_id;
        snap.snapshot.tick = state.last_input.tick;
        snap.snapshot.sequence = state.next_snapshot_sequence++;
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

    for (auto &[subject_peer, subject] : clients) {
        (void)subject_peer;
        const uint32_t state_sequence = subject.next_player_state_sequence++;
        for (auto &[observer_peer, observer] : clients) {
            if (!should_replicate_player_state(observer, subject)) {
                continue;
            }
            PlayerStatePacket packet{};
            packet.header = net_make_header(
                NetMsgType::PlayerState,
                static_cast<uint16_t>(sizeof(packet.state)),
                next_packet_sequence++);
            packet.state = subject.state;
            packet.state.sequence = state_sequence;

            ENetPacket *out = enet_packet_create(&packet, sizeof(packet), 0);
            enet_peer_send(observer_peer, static_cast<uint8_t>(NetChannel::Unreliable), out);
            record_tx(sizeof(packet));
            debug_counters.player_state_broadcasts_window += 1;
        }
    }
}

void NetServer::broadcast_player_remove(uint32_t player_id) {
    if (!server || player_id == 0) {
        return;
    }
    PlayerRemovePacket packet{};
    packet.header = net_make_header(
        NetMsgType::PlayerRemove,
        static_cast<uint16_t>(sizeof(packet.payload)),
        next_packet_sequence++);
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
    next_packet_sequence = 1;
    server_sim_tick = 0;
    last_pump_ms = 0;
    sim_accumulator_ms = 0.0;
    last_snapshot_send_ms = 0;
    debug_counters = DebugCounters{};
    refresh_debug_stats();
    return true;
}

void NetServer::shutdown() {
    clients.clear();
    server_sim_tick = 0;
    last_pump_ms = 0;
    sim_accumulator_ms = 0.0;
    last_snapshot_send_ms = 0;
    last_player_broadcast_ms = 0;
    next_packet_sequence = 1;
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
            state.state.tick = server_sim_tick;
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
            assign.header = net_make_header(
                NetMsgType::AssignPlayer,
                static_cast<uint16_t>(sizeof(assign.payload)),
                next_packet_sequence++);
            assign.payload.player_id = state.player_id;
            ENetPacket *out = enet_packet_create(&assign, sizeof(assign), ENET_PACKET_FLAG_RELIABLE);
            enet_peer_send(event.peer, static_cast<uint8_t>(NetChannel::Reliable), out);
            record_tx(sizeof(assign));

            ProtocolInfoPacket proto{};
            proto.header = net_make_header(
                NetMsgType::ProtocolInfo,
                static_cast<uint16_t>(sizeof(proto.payload)),
                next_packet_sequence++);
            proto.payload.protocol_version = k_net_protocol_version;
            proto.payload.feature_flags =
                net_feature(NetFeatureFlags::InterestFilteredReplication) |
                net_feature(NetFeatureFlags::ChunkStreaming);
            proto.payload.server_tick_hz = 60;
            ENetPacket *proto_packet = enet_packet_create(&proto, sizeof(proto), ENET_PACKET_FLAG_RELIABLE);
            enet_peer_send(event.peer, static_cast<uint8_t>(NetChannel::Reliable), proto_packet);
            record_tx(sizeof(proto));
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
