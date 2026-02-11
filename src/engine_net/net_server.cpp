#include "engine_net/net_server.hpp"

#include <enet/enet.h>
#include <cstring>

struct InputPacket {
    NetMsgType type = NetMsgType::Input;
    NetTickInput input{};
};

struct SnapshotPacket {
    NetMsgType type = NetMsgType::Snapshot;
    NetSnapshot snapshot{};
};

void NetServer::init(uint16_t port) {
    enet_initialize();
    ENetAddress address{};
    address.host = ENET_HOST_ANY;
    address.port = port;
    server = enet_host_create(&address, 32, 2, 0, 0);
}

void NetServer::shutdown() {
    if (server) {
        enet_host_destroy(server);
        server = nullptr;
    }
    enet_deinitialize();
}

void NetServer::pump() {
    ENetEvent event{};
    while (enet_host_service(server, &event, 0) > 0) {
        switch (event.type) {
        case ENET_EVENT_TYPE_RECEIVE:
            if (event.packet->dataLength >= sizeof(InputPacket)) {
                InputPacket packet{};
                std::memcpy(&packet, event.packet->data, sizeof(InputPacket));
                if (packet.type == NetMsgType::Input) {
                    sim_x += packet.input.move_x * 0.05f;

                    SnapshotPacket snap{};
                    snap.snapshot.tick = packet.input.tick;
                    snap.snapshot.x = sim_x;
                    ENetPacket *out = enet_packet_create(&snap, sizeof(snap), 0);
                    enet_peer_send(event.peer, 0, out);
                }
            }
            enet_packet_destroy(event.packet);
            break;
        default:
            break;
        }
    }
}
