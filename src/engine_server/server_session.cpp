#include "engine_server/server_session.hpp"

#include "engine_net/net_runtime_shared.hpp"
#include "engine_gameplay/player/player_controller.hpp"
#include "engine_input/input_state.hpp"
#include "engine_world/world_gen.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtx/quaternion.hpp>

namespace {
constexpr float k_server_tick_dt = 1.0f / 60.0f;

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

ServerSession::ServerSession() { reset(); }

void ServerSession::reset() {
    client_states.clear();
    world_chunk = VoxelChunk{};
    world_chunk.generate_spherical_planet_seeded(k_voxov_flat_world_seed);
    collision_world = VoxelCollisionWorld(&world_chunk);
    next_player_id = 1;
    server_sim_tick_value = 0;
}

ServerClientState &ServerSession::connect_client(_ENetPeer *peer) {
    ServerClientState state{};
    state.player_id = next_player_id++;
    state.player = PlayerControllerSystem::spawn_player(collision_world);
    state.player.network_id = state.player_id;
    const glm::vec2 spawn_offset = server_spawn_offset(state.player_id);
    state.player.transform.position.x += spawn_offset.x;
    state.player.transform.position.z += spawn_offset.y;
    state.player.transform.position.y =
        std::max(2.0f,
                 collision_world.find_spawn_height(
                     glm::vec2(state.player.transform.position.x,
                               state.player.transform.position.z),
                     state.player.controller.capsuleRadius,
                     state.player.controller.capsuleHeight) +
                     0.05f);
    state.player.locomotion.facing_yaw_deg = state.player.camera_rig.yaw;
    state.player.locomotion.desired_yaw_deg = state.player.camera_rig.yaw;
    state.player.transform.rotation =
        glm::angleAxis(state.player.camera_rig.yaw * 0.01745329251994329577f,
                       glm::vec3(0.0f, 1.0f, 0.0f));
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
        state.player, input, collision_world, k_server_tick_dt, false);
    sync_net_player_state_from_entity(
        state.player, state.player_id, server_sim_tick_value, state.state);
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
    const int32_t subject_chunk_x =
        static_cast<int32_t>(std::floor(subject.state.x / k_chunk_world_size));
    const int32_t subject_chunk_z =
        static_cast<int32_t>(std::floor(subject.state.z / k_chunk_world_size));
    const int32_t dx_chunks =
        std::abs(subject_chunk_x - observer.interest.center_x);
    const int32_t dz_chunks =
        std::abs(subject_chunk_z - observer.interest.center_z);
    const int32_t allowed_chunk_delta =
        static_cast<int32_t>(observer.interest.radius) + 1;
    if (dx_chunks <= allowed_chunk_delta && dz_chunks <= allowed_chunk_delta) {
        return true;
    }

    const float dx = subject.state.x - observer.state.x;
    const float dz = subject.state.z - observer.state.z;
    const float max_distance =
        std::max(48.0f, (static_cast<float>(observer.interest.radius) + 1.0f) *
                            k_chunk_world_size * 1.5f);
    return ((dx * dx) + (dz * dz)) <= (max_distance * max_distance);
}

uint32_t ServerSession::server_sim_tick() const { return server_sim_tick_value; }
