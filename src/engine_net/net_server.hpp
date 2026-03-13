#pragma once

#include "engine_net/net_common.hpp"

#include <cstdint>
#include <memory>

struct _ENetHost;
struct _ENetPeer;
class ServerSession;
struct ServerClientState;

class NetServer {
public:
    NetServer();
    ~NetServer();

    NetServer(const NetServer &) = delete;
    NetServer &operator=(const NetServer &) = delete;

    bool init(uint16_t port, bool loopback_only = false);
    void shutdown();
    void pump();
    uint16_t bound_port() const;
    NetDebugStats debug_stats() const;

private:
    void send_session_info(_ENetPeer *peer);
    void broadcast_session_info();
    void send_chunk_state(
        _ENetPeer *peer,
        ServerClientState &state,
        NetChunkCoord coord,
        uint32_t version);
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
    std::unique_ptr<ServerSession> session;
    uint32_t next_packet_sequence = 1;
    uint64_t last_pump_ms = 0;
    double sim_accumulator_ms = 0.0;
    uint64_t last_snapshot_send_ms = 0;
    uint64_t last_player_broadcast_ms = 0;
    DebugCounters debug_counters{};
};
