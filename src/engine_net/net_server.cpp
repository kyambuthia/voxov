#include "engine_net/net_server.hpp"
#include "engine_gameplay/animation/player_animation_graph.hpp"
#include "engine_gameplay/player/player_controller.hpp"
#include "engine_input/input_state.hpp"
#include "engine_world/world_gen.hpp"

#include <enet/enet.h>
#include <spdlog/spdlog.h>

#include <cstring>
#include <cstdio>
#include <cmath>
#include <chrono>
#include <algorithm>

#include <glm/gtx/quaternion.hpp>

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

struct SessionInfoPacket {
    NetPacketHeader header{};
    NetSessionInfo payload{};
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
constexpr double kServerSimTickMs = 1000.0 / 60.0;
constexpr uint64_t kSnapshotSendIntervalMs = 33;
constexpr uint64_t kPlayerBroadcastIntervalMs = 33;
constexpr int kMaxCatchupTicksPerPump = 8;

uint64_t now_ms() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

void sync_net_player_state_from_entity(
    const PlayerEntity &player,
    uint32_t player_id,
    uint32_t server_tick,
    NetPlayerState &state) {
    state.player_id = player_id;
    state.tick = server_tick;
    state.x = player.transform.position.x;
    state.y = player.transform.position.y;
    state.z = player.transform.position.z;
    state.vx = player.controller.velocity.x;
    state.vy = player.controller.velocity.y;
    state.vz = player.controller.velocity.z;
    state.anim_state = static_cast<uint8_t>(player.anim_state);
    state.anim_phase = player.anim_phase;
    state.anim_blend = player.anim_blend;
}

glm::vec2 server_spawn_offset(uint32_t player_id) {
    switch (player_id % 4u) {
    case 1u:
        return glm::vec2(-1.5f, 0.0f);
    case 2u:
        return glm::vec2(1.5f, 0.0f);
    case 3u:
        return glm::vec2(0.0f, 1.5f);
    default:
        return glm::vec2(0.0f, -1.5f);
    }
}
}

int32_t NetServer::chunk_key(NetChunkCoord coord) const {
    return (static_cast<int32_t>(coord.x) << 16) ^ static_cast<uint16_t>(coord.z);
}

NetSessionInfo NetServer::make_session_info() const {
    NetSessionInfo info{};
    net_copy_cstr(info.server_name, local_only ? "VOXOV Local" : "VOXOV Host");
    info.world_seed = k_voxov_flat_world_seed;
    info.current_players = static_cast<uint16_t>(clients.size());
    info.max_players = 32;
    if (local_only) {
        info.flags |= net_session_flag(NetSessionFlags::LoopbackOnly);
    } else {
        info.flags |= net_session_flag(NetSessionFlags::LanAdvertised);
    }
    return info;
}

void NetServer::send_session_info(ENetPeer *peer) {
    if (!server || !peer) {
        return;
    }
    SessionInfoPacket packet{};
    packet.header = net_make_header(
        NetMsgType::SessionInfo,
        static_cast<uint16_t>(sizeof(packet.payload)),
        next_packet_sequence++);
    packet.payload = make_session_info();
    ENetPacket *out = enet_packet_create(&packet, sizeof(packet), ENET_PACKET_FLAG_RELIABLE);
    enet_peer_send(peer, static_cast<uint8_t>(NetChannel::Reliable), out);
    record_tx(sizeof(packet));
}

void NetServer::broadcast_session_info() {
    if (!server || clients.empty()) {
        return;
    }
    for (auto &[peer_ptr, state] : clients) {
        (void)state;
        send_session_info(peer_ptr);
    }
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
    InputState input{};
    input.move.x = state.last_input.move_x;
    input.move.y = state.last_input.move_y;
    input.jump_held = net_flag_set(state.last_input.action_flags, NetInputFlags::JumpHeld);
    input.jump_pressed = net_flag_set(state.last_input.action_flags, NetInputFlags::JumpPressed);
    input.sprint_held = net_flag_set(state.last_input.action_flags, NetInputFlags::SprintHeld);
    input.crouch_held = net_flag_set(state.last_input.action_flags, NetInputFlags::CrouchHeld);
    if (input.jump_pressed) {
        state.last_input.action_flags &= static_cast<uint8_t>(~net_flag(NetInputFlags::JumpPressed));
    }
    state.player.camera_rig.yaw = state.last_input.camera_yaw_deg;

    (void)PlayerControllerSystem::simulate_fixed(
        state.player,
        input,
        collision_world,
        kServerTickDt,
        false);
    sync_net_player_state_from_entity(state.player, state.player_id, server_sim_tick, state.state);
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
    generate_flat_world_locomotion_chunk(world_chunk);
    collision_world = VoxelCollisionWorld(&world_chunk);
    debug_counters = DebugCounters{};
    refresh_debug_stats();
    return true;
}

void NetServer::shutdown() {
    clients.clear();
    collision_world = VoxelCollisionWorld(nullptr);
    world_chunk = VoxelChunk{};
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
            state.player = PlayerControllerSystem::spawn_player(collision_world);
            state.player.network_id = state.player_id;
            const glm::vec2 spawn_offset = server_spawn_offset(state.player_id);
            state.player.transform.position.x += spawn_offset.x;
            state.player.transform.position.z += spawn_offset.y;
            state.player.transform.position.y = collision_world.find_spawn_height(
                glm::vec2(state.player.transform.position.x, state.player.transform.position.z),
                state.player.controller.capsuleRadius,
                state.player.controller.capsuleHeight) + 0.05f;
            state.player.locomotion.facing_yaw_deg = state.player.camera_rig.yaw;
            state.player.locomotion.desired_yaw_deg = state.player.camera_rig.yaw;
            state.player.transform.rotation = glm::angleAxis(
                state.player.camera_rig.yaw * 0.01745329251994329577f,
                glm::vec3(0.0f, 1.0f, 0.0f));
            sync_net_player_state_from_entity(state.player, state.player_id, server_sim_tick, state.state);
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
            broadcast_session_info();
            break;
        }
        case ENET_EVENT_TYPE_DISCONNECT:
        {
            const auto it = clients.find(event.peer);
            if (it != clients.end()) {
                const uint32_t removed_player_id = it->second.player_id;
                clients.erase(it);
                broadcast_session_info();
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

uint16_t NetServer::bound_port() const {
    if (!server) {
        return 0;
    }
    return server->address.port;
}

NetDebugStats NetServer::debug_stats() const {
    return debug_counters.snapshot;
}
