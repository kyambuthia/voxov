#pragma once

#include "engine_net/net_common.hpp"

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
    bool poll_snapshot(NetSnapshot &out_snapshot);

private:
    _ENetHost *client = nullptr;
    _ENetPeer *peer = nullptr;
    bool has_snapshot = false;
    NetSnapshot latest_snapshot{};
};
