#pragma once

#include "engine_gameplay/player/player_components.hpp"
#include "engine_net_proto/net_types.hpp"
#include "engine_world/physics/voxel_collision.hpp"
#include "engine_world/voxel_chunk.hpp"

#include <cstdint>
#include <optional>
#include <unordered_map>

struct _ENetPeer;

struct ServerClientState {
    uint32_t player_id = 0;
    uint32_t next_player_state_sequence = 1;
    uint32_t next_snapshot_sequence = 1;
    PlayerEntity player{};
    NetPlayerState state{};
    NetTickInput last_input{};
    NetChunkInterest interest{};
    std::unordered_map<int32_t, uint32_t> sent_chunks;
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
    NetSessionInfo make_session_info(bool local_only) const;
    bool should_replicate_player_state(
        const ServerClientState &observer,
        const ServerClientState &subject) const;
    uint32_t server_sim_tick() const;

private:
    void simulate_client_tick(ServerClientState &state);

    std::unordered_map<_ENetPeer *, ServerClientState> client_states;
    VoxelChunk world_chunk{};
    VoxelCollisionWorld collision_world{nullptr};
    uint32_t next_player_id = 1;
    uint32_t server_sim_tick_value = 0;
};
