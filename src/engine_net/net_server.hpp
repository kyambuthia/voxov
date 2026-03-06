#pragma once

#include "engine_gameplay/player/player_components.hpp"
#include "engine_net/net_common.hpp"
#include "engine_world/physics/voxel_collision.hpp"
#include "engine_world/voxel_chunk.hpp"

#include <cstddef>
#include <cstdint>
#include <unordered_map>

struct _ENetHost;
struct _ENetPeer;

class NetServer {
public:
    bool init(uint16_t port, bool loopback_only = false);
    void shutdown();
    void pump();
    NetDebugStats debug_stats() const;

private:
    struct ClientState {
        uint32_t player_id = 0;
        uint32_t next_player_state_sequence = 1;
        uint32_t next_snapshot_sequence = 1;
        PlayerEntity player{};
        NetPlayerState state{};
        NetTickInput last_input{};
        NetChunkInterest interest{};
        std::unordered_map<int32_t, uint32_t> sent_chunks;
    };

    int32_t chunk_key(NetChunkCoord coord) const;
    bool should_replicate_player_state(const ClientState &observer, const ClientState &subject) const;
    void send_chunk_state(_ENetPeer *peer, ClientState &state, NetChunkCoord coord, uint32_t version);
    void simulate_client_tick(ClientState &state);
    void simulate_fixed_tick();
    void send_snapshots();
    void broadcast_player_states();
    void broadcast_player_remove(uint32_t player_id);
    void record_tx(size_t bytes, uint32_t packet_count = 1);
    void record_rx(size_t bytes);
    void refresh_debug_stats();

    struct DebugCounters {
        uint64_t tx_packets_total = 0;
        uint64_t tx_bytes_total = 0;
        uint64_t rx_packets_total = 0;
        uint64_t rx_bytes_total = 0;
        uint64_t invalid_packets_total = 0;
        uint32_t tx_packets_window = 0;
        uint32_t tx_bytes_window = 0;
        uint32_t rx_packets_window = 0;
        uint32_t rx_bytes_window = 0;
        uint32_t snapshots_sent_window = 0;
        uint32_t player_state_broadcasts_window = 0;
        uint64_t last_rollup_ms = 0;
        NetDebugStats snapshot{};
    };

    bool initialized = false;
    bool local_only = false;
    _ENetHost *server = nullptr;
    std::unordered_map<_ENetPeer *, ClientState> clients;
    VoxelChunk world_chunk{};
    VoxelCollisionWorld collision_world{nullptr};
    uint32_t next_player_id = 1;
    uint32_t next_packet_sequence = 1;
    uint32_t server_sim_tick = 0;
    uint64_t last_pump_ms = 0;
    double sim_accumulator_ms = 0.0;
    uint64_t last_snapshot_send_ms = 0;
    uint64_t last_player_broadcast_ms = 0;
    DebugCounters debug_counters{};
};
