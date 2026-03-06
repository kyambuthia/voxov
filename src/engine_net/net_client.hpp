#pragma once

#include "engine_net/net_common.hpp"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

struct _ENetHost;
struct _ENetPeer;

enum class NetClientConnectionState : uint8_t {
    Disconnected = 0,
    Connecting = 1,
    Connected = 2
};

class NetClient {
public:
    bool init();
    bool connect(const char *host, uint16_t port);
    void disconnect();
    void shutdown();
    void pump();
    void send_input(const NetTickInput &input);
    void set_chunk_interest(const NetChunkInterest &interest);
    bool poll_snapshot(NetSnapshot &out_snapshot);
    bool poll_chunk_state(NetChunkState &out_state);
    uint32_t local_player_id() const;
    NetProtocolInfo protocol_info() const;
    bool has_session_info() const;
    NetSessionInfo session_info() const;
    bool is_connected() const;
    bool is_initialized() const;
    NetClientConnectionState connection_state() const;
    const std::string &connect_target_host() const;
    uint16_t connect_target_port() const;
    NetDebugStats debug_stats() const;
    const std::unordered_map<uint32_t, NetPlayerState> &player_states() const;

private:
    void record_tx(size_t bytes);
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
        uint64_t last_rollup_ms = 0;
        NetDebugStats snapshot{};
    };

    bool initialized = false;
    bool connected = false;
    _ENetHost *client = nullptr;
    _ENetPeer *peer = nullptr;
    bool has_snapshot = false;
    NetSnapshot latest_snapshot{};
    uint32_t last_snapshot_sequence = 0;
    bool has_last_snapshot_sequence = false;
    std::vector<NetChunkState> chunk_updates;
    uint32_t assigned_player_id = 0;
    std::unordered_map<uint32_t, NetPlayerState> replicated_players;
    std::unordered_map<uint32_t, uint32_t> replicated_player_sequences;
    NetProtocolInfo server_protocol_info{};
    bool has_server_session_info = false;
    NetSessionInfo server_session_info{};
    bool has_pending_interest = false;
    NetChunkInterest pending_interest{};
    NetClientConnectionState state = NetClientConnectionState::Disconnected;
    std::string target_host;
    uint16_t target_port = 0;
    uint32_t next_packet_sequence = 1;
    DebugCounters debug_counters{};
};
