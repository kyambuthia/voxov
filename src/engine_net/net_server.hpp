#pragma once

#include "engine_net/net_common.hpp"

#include <unordered_map>

struct _ENetHost;
struct _ENetPeer;

class NetServer {
public:
    void init(uint16_t port);
    void shutdown();
    void pump();

private:
    struct ClientState {
        float sim_x = 0.0f;
        NetChunkInterest interest{};
        std::unordered_map<int32_t, uint32_t> sent_chunks;
    };

    int32_t chunk_key(NetChunkCoord coord) const;
    void send_chunk_state(_ENetPeer *peer, ClientState &state, NetChunkCoord coord, uint32_t version);

    _ENetHost *server = nullptr;
    std::unordered_map<_ENetPeer *, ClientState> clients;
};
