#include "engine_server/server_session.hpp"

#include "engine/planet_gameplay_config.hpp"
#include "engine_net/net_runtime_shared.hpp"
#include "engine_gameplay/player/player_controller.hpp"
#include "engine_input/input_state.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtx/quaternion.hpp>

namespace {
constexpr float k_server_tick_dt = 1.0f / 60.0f;
constexpr uint64_t k_interest_request_interval_ms = 50;
constexpr uint8_t k_known_input_flags =
    net_flag(NetInputFlags::JumpHeld) |
    net_flag(NetInputFlags::JumpPressed) |
    net_flag(NetInputFlags::SprintHeld) |
    net_flag(NetInputFlags::CrouchHeld);

bool supported_body(uint32_t body_id) {
    return body_id == 1u || body_id == 3u;
}

bool tick_newer(uint32_t incoming, uint32_t last_seen) {
    return incoming != last_seen &&
           static_cast<int32_t>(incoming - last_seen) > 0;
}

BlockWorldConfig server_world_config(double radius, uint64_t seed) {
    PlanetDefinition planet{};
    planet.center = glm::dvec3(0.0);
    planet.radius = radius;
    planet.voxel_size = kPlayablePlanetConfig.voxel_size_m;
    planet.chunks_per_face = 64;
    planet.seed = seed;

    BlockWorldConfig config{};
    config.planet = planet;
    config.surface_shells = kPlayablePlanetConfig.surface_shells;
    config.base_resolution = kPlayablePlanetConfig.base_resolution;
    config.block_size = kPlayablePlanetConfig.voxel_size_m;
    config.terrain_feature_size =
        kPlayablePlanetConfig.terrain_feature_size_m;
    config.terrain_base_height =
        kPlayablePlanetConfig.terrain_base_height_blocks;
    config.terrain_amplitude =
        kPlayablePlanetConfig.terrain_amplitude_blocks;
    config.terrain_min_height =
        kPlayablePlanetConfig.terrain_min_height_blocks;
    config.terrain_max_height =
        kPlayablePlanetConfig.terrain_max_height_blocks;
    config.terrain_shell_margin =
        kPlayablePlanetConfig.terrain_shell_margin_blocks;
    config.chunk_size = kPlayablePlanetConfig.chunk_size;
    config.seed = seed;
    return config;
}

float server_surface_radius(const BlockWorld &world) {
    const int32_t shell = world.shell_count() - 1;
    const int32_t equator_col = world.shell_config(shell).horizontal_res / 2;
    return static_cast<float>(glm::length(world.spawn_position_at_face_uv(
        PlanetFace::PosX, equator_col, equator_col, 0.0)));
}

void sync_net_player_state_from_entity(const PlayerEntity &player,
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
} // namespace

size_t NetChunkCoordHash::operator()(const NetChunkCoord &coord) const noexcept {
    size_t hash = static_cast<size_t>(coord.body_id);
    const auto mix = [&hash](size_t value) {
        hash ^= value + static_cast<size_t>(0x9e3779b9u) + (hash << 6u) +
                (hash >> 2u);
    };
    mix(coord.face);
    mix(coord.shell);
    mix(static_cast<uint16_t>(coord.x));
    mix(static_cast<uint16_t>(coord.y));
    mix(static_cast<uint16_t>(coord.z));
    return hash;
}

ServerSession::ServerSession() { reset(); }

void ServerSession::reset() {
    client_states.clear();
    voxov_world = BlockWorld{};
    voxov_world.init(server_world_config(
        kPlayablePlanetConfig.radius_m, k_voxov_flat_world_seed));
    voxov_collision = VoxelCollisionWorld{};
    voxov_collision.set_planet_surface_collider(
        glm::vec3(0.0f), server_surface_radius(voxov_world));

    aster_world = BlockWorld{};
    aster_world.init(server_world_config(
        32.0, k_voxov_flat_world_seed ^ 0xa57e'c0deull));
    aster_collision = VoxelCollisionWorld{};
    aster_collision.set_planet_surface_collider(
        glm::vec3(0.0f), server_surface_radius(aster_world));
    next_player_id = 1;
    server_sim_tick_value = 0;
}

ServerClientState &ServerSession::connect_client(_ENetPeer *peer) {
    ServerClientState state{};
    state.player_id = next_player_id++;
    state.player = spawn_player_for_body(1u, state.player_id);
    sync_net_player_state_from_entity(
        state.player, state.player_id, server_sim_tick_value, state.state);

    auto [it, inserted] = client_states.insert_or_assign(peer, std::move(state));
    (void)inserted;
    return it->second;
}

std::optional<uint32_t> ServerSession::disconnect_client(_ENetPeer *peer) {
    const auto it = client_states.find(peer);
    if (it == client_states.end()) {
        return std::nullopt;
    }
    const uint32_t player_id = it->second.player_id;
    client_states.erase(it);
    return player_id;
}

ServerClientState *ServerSession::find_client(_ENetPeer *peer) {
    const auto it = client_states.find(peer);
    return it != client_states.end() ? &it->second : nullptr;
}

const std::unordered_map<_ENetPeer *, ServerClientState> &
ServerSession::clients() const {
    return client_states;
}

std::unordered_map<_ENetPeer *, ServerClientState> &
ServerSession::clients() {
    return client_states;
}

void ServerSession::simulate_client_tick(ServerClientState &state) {
    InputState input{};
    input.move.x = state.last_input.move_x;
    input.move.y = state.last_input.move_y;
    input.jump_held =
        net_flag_set(state.last_input.action_flags, NetInputFlags::JumpHeld);
    input.jump_pressed =
        net_flag_set(state.last_input.action_flags, NetInputFlags::JumpPressed);
    input.sprint_held =
        net_flag_set(state.last_input.action_flags, NetInputFlags::SprintHeld);
    input.crouch_held =
        net_flag_set(state.last_input.action_flags, NetInputFlags::CrouchHeld);
    if (input.jump_pressed) {
        state.last_input.action_flags &=
            static_cast<uint8_t>(~net_flag(NetInputFlags::JumpPressed));
    }
    state.player.camera_rig.yaw = state.last_input.camera_yaw_deg;

    (void)PlayerControllerSystem::simulate_fixed(
        state.player, input, collision_for_body(state.state.world.body_id),
        k_server_tick_dt, false);
    sync_net_player_state_from_entity(
        state.player, state.player_id, server_sim_tick_value, state.state);
}

bool ServerSession::accept_input(ServerClientState &client,
                                 const NetTickInput &input) {
    if (!supported_body(input.body_id) || !std::isfinite(input.move_x) ||
        !std::isfinite(input.move_y) ||
        !std::isfinite(input.camera_yaw_deg) ||
        std::abs(input.move_x) > 1.0f || std::abs(input.move_y) > 1.0f ||
        std::abs(input.camera_yaw_deg) > 36000.0f ||
        (input.action_flags & static_cast<uint8_t>(~k_known_input_flags)) != 0 ||
        (client.has_last_input_tick &&
         !tick_newer(input.tick, client.last_input.tick))) {
        return false;
    }

    const uint32_t previous_body = client.state.world.body_id;
    if (input.body_id != previous_body) {
        client.player = spawn_player_for_body(input.body_id, client.player_id);
    }
    client.last_input = input;
    client.has_last_input_tick = true;
    client.state.world.body_id = input.body_id;
    return true;
}

PlayerEntity ServerSession::spawn_player_for_body(uint32_t body_id,
                                                  uint32_t player_id) const {
    const BlockWorld &world = body_id == 3u ? aster_world : voxov_world;
    const VoxelCollisionWorld &collision = collision_for_body(body_id);
    const int32_t shell = world.shell_count() - 1;
    const int32_t equator_col = world.shell_config(shell).horizontal_res / 2;
    PlayerEntity player = PlayerControllerSystem::spawn_on_planet_surface(
        world, collision, PlanetFace::PosX, equator_col, equator_col, 2.0);
    player.network_id = player_id;
    player.controller.capsuleRadius = 0.7f;
    const glm::vec2 offset = server_spawn_offset(player_id);
    player.transform.position.y += offset.x;
    player.transform.position.z += offset.y;
    player.locomotion.facing_yaw_deg = player.camera_rig.yaw;
    player.locomotion.desired_yaw_deg = player.camera_rig.yaw;
    player.transform.rotation = glm::angleAxis(
        player.camera_rig.yaw * 0.01745329251994329577f,
        collision.planet_up_at(player.transform.position));
    return player;
}

const VoxelCollisionWorld &
ServerSession::collision_for_body(uint32_t body_id) const {
    return body_id == 3u ? aster_collision : voxov_collision;
}

bool ServerSession::accept_interest(ServerClientState &client,
                                    const NetChunkInterest &interest,
                                    uint64_t received_at_ms) {
    if (!supported_body(interest.body_id) || interest.face >= 6u ||
        interest.shell >= 32u || interest.radius > k_net_max_interest_radius) {
        return false;
    }
    if (client.last_interest_request_ms != 0 &&
        received_at_ms - client.last_interest_request_ms <
            k_interest_request_interval_ms) {
        return false;
    }
    client.interest = interest;
    client.last_interest_request_ms = received_at_ms;
    return true;
}

void ServerSession::simulate_fixed_tick() {
    server_sim_tick_value += 1;
    for (auto &[peer, state] : client_states) {
        (void)peer;
        simulate_client_tick(state);
    }
}

NetSessionInfo ServerSession::make_session_info(bool local_only) const {
    NetSessionInfo info{};
    net_copy_cstr(info.server_name, local_only ? "VOXOV Local" : "VOXOV Host");
    info.world_seed = k_voxov_flat_world_seed;
    info.current_players = static_cast<uint16_t>(client_states.size());
    info.max_players = 32;
    if (local_only) {
        info.flags |= net_session_flag(NetSessionFlags::LoopbackOnly);
    } else {
        info.flags |= net_session_flag(NetSessionFlags::LanAdvertised);
    }
    return info;
}

bool ServerSession::should_replicate_player_state(
    const ServerClientState &observer,
    const ServerClientState &subject) const {
    if (observer.player_id == 0 || subject.player_id == 0) {
        return false;
    }
    if (observer.player_id == subject.player_id) {
        return false;
    }

    constexpr float k_chunk_world_size = 16.0f;
    if (observer.interest.body_id != subject.state.world.body_id ||
        observer.state.world.body_id != subject.state.world.body_id) {
        return false;
    }
    const int32_t subject_chunk_x =
        static_cast<int32_t>(std::floor(subject.state.x / k_chunk_world_size));
    const int32_t subject_chunk_y =
        static_cast<int32_t>(std::floor(subject.state.y / k_chunk_world_size));
    const int32_t subject_chunk_z =
        static_cast<int32_t>(std::floor(subject.state.z / k_chunk_world_size));
    const int32_t dx_chunks =
        std::abs(subject_chunk_x - observer.interest.center_x);
    const int32_t dz_chunks =
        std::abs(subject_chunk_z - observer.interest.center_z);
    const int32_t dy_chunks =
        std::abs(subject_chunk_y - observer.interest.center_y);
    const int32_t allowed_chunk_delta =
        static_cast<int32_t>(observer.interest.radius) + 1;
    if (dx_chunks <= allowed_chunk_delta &&
        dy_chunks <= allowed_chunk_delta &&
        dz_chunks <= allowed_chunk_delta) {
        return true;
    }

    const double dx = subject.state.x - observer.state.x;
    const double dy = subject.state.y - observer.state.y;
    const double dz = subject.state.z - observer.state.z;
    const double max_distance =
        std::max(48.0f, (static_cast<float>(observer.interest.radius) + 1.0f) *
                            k_chunk_world_size * 1.5f);
    return ((dx * dx) + (dy * dy) + (dz * dz)) <=
           (max_distance * max_distance);
}

uint32_t ServerSession::server_sim_tick() const { return server_sim_tick_value; }
