#pragma once

#include "engine_net/net_common.hpp"

#include <unordered_map>
#include <vector>

struct _ENetHost;
struct _ENetPeer;

class NetClient {
public:
    void init();
    void connect(const char *host, uint16_t port);
    void disconnect();
    void shutdown();
    void pump();
    void send_input(const NetTickInput &input);
    void set_chunk_interest(const NetChunkInterest &interest);
    bool poll_snapshot(NetSnapshot &out_snapshot);
    bool poll_chunk_state(NetChunkState &out_state);
    uint32_t local_player_id() const;
    const std::unordered_map<uint32_t, NetPlayerState> &player_states() const;

private:
    _ENetHost *client = nullptr;
    _ENetPeer *peer = nullptr;
    bool has_snapshot = false;
    NetSnapshot latest_snapshot{};
    std::vector<NetChunkState> chunk_updates;
    uint32_t assigned_player_id = 0;
    std::unordered_map<uint32_t, NetPlayerState> replicated_players;
};
