#pragma once

#include "engine_gameplay/player/player_components.hpp"
#include "engine_net_proto/net_types.hpp"
#include "engine_world/physics/voxel_collision.hpp"
#include "engine_world/planet_blocks.hpp"

#include <cstdint>
#include <cstddef>
#include <optional>
#include <unordered_map>

struct _ENetPeer;

struct NetChunkCoordHash {
    size_t operator()(const NetChunkCoord &coord) const noexcept;
};

struct ServerClientState {
    uint32_t player_id = 0;
    uint32_t next_player_state_sequence = 1;
    uint32_t next_snapshot_sequence = 1;
    PlayerEntity player{};
    NetPlayerState state{};
    NetTickInput last_input{};
    bool has_last_input_tick = false;
    uint64_t last_interest_request_ms = 0;
    NetChunkInterest interest{};
    std::unordered_map<NetChunkCoord, uint32_t, NetChunkCoordHash> sent_chunks;
};

class ServerSession {
public:
    ServerSession();

    void reset();
    ServerClientState &connect_client(_ENetPeer *peer);
    std::optional<uint32_t> disconnect_client(_ENetPeer *peer);
    ServerClientState *find_client(_ENetPeer *peer);

    const std::unordered_map<_ENetPeer *, ServerClientState> &clients() const;
    std::unordered_map<_ENetPeer *, ServerClientState> &clients();

    void simulate_fixed_tick();
    bool accept_input(ServerClientState &client, const NetTickInput &input);
    bool accept_interest(ServerClientState &client,
                         const NetChunkInterest &interest,
                         uint64_t received_at_ms);
    NetSessionInfo make_session_info(bool local_only) const;
    bool should_replicate_player_state(
        const ServerClientState &observer,
        const ServerClientState &subject) const;
    uint32_t server_sim_tick() const;

private:
    void simulate_client_tick(ServerClientState &state);
    PlayerEntity spawn_player_for_body(uint32_t body_id,
                                       uint32_t player_id) const;
    const VoxelCollisionWorld &collision_for_body(uint32_t body_id) const;

    std::unordered_map<_ENetPeer *, ServerClientState> client_states;
    BlockWorld voxov_world{};
    BlockWorld aster_world{};
    VoxelCollisionWorld voxov_collision{nullptr};
    VoxelCollisionWorld aster_collision{nullptr};
    uint32_t next_player_id = 1;
    uint32_t server_sim_tick_value = 0;
};
