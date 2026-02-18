#pragma once

#include "engine_net/net_common.hpp"

#include <unordered_map>
#include <cstdint>

struct _ENetHost;
struct _ENetPeer;

class NetServer {
public:
    void init(uint16_t port, bool loopback_only = false);
    void shutdown();
    void pump();

private:
    struct ClientState {
        uint32_t player_id = 0;
        NetPlayerState state{};
        NetTickInput last_input{};
        NetChunkInterest interest{};
        std::unordered_map<int32_t, uint32_t> sent_chunks;
    };

    int32_t chunk_key(NetChunkCoord coord) const;
    void send_chunk_state(_ENetPeer *peer, ClientState &state, NetChunkCoord coord, uint32_t version);
    void broadcast_player_states();

    bool initialized = false;
    bool local_only = false;
    _ENetHost *server = nullptr;
    std::unordered_map<_ENetPeer *, ClientState> clients;
    uint32_t next_player_id = 1;
};
