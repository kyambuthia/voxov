#pragma once

#include "engine_net/net_common.hpp"

struct _ENetHost;
struct _ENetPeer;

class NetServer {
public:
    void init(uint16_t port);
    void shutdown();
    void pump();

private:
    _ENetHost *server = nullptr;
    float sim_x = 0.0f;
};
