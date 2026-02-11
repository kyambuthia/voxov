#include "engine_net/net_client.hpp"

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

void NetClient::init() {
    enet_initialize();
    client = enet_host_create(nullptr, 1, 2, 0, 0);
}

void NetClient::connect(const char *host, uint16_t port) {
    ENetAddress address{};
    enet_address_set_host(&address, host);
    address.port = port;
    peer = enet_host_connect(client, &address, 2, 0);
}

void NetClient::disconnect() {
    if (peer) {
        enet_peer_disconnect(peer, 0);
        peer = nullptr;
    }
}

void NetClient::shutdown() {
    if (client) {
        enet_host_destroy(client);
        client = nullptr;
    }
    enet_deinitialize();
}

void NetClient::pump() {
    ENetEvent event{};
    while (enet_host_service(client, &event, 0) > 0) {
        switch (event.type) {
        case ENET_EVENT_TYPE_RECEIVE:
            if (event.packet->dataLength >= sizeof(SnapshotPacket)) {
                SnapshotPacket packet{};
                std::memcpy(&packet, event.packet->data, sizeof(SnapshotPacket));
                if (packet.type == NetMsgType::Snapshot) {
                    latest_snapshot = packet.snapshot;
                    has_snapshot = true;
                }
            }
            enet_packet_destroy(event.packet);
            break;
        default:
            break;
        }
    }
}

void NetClient::send_input(const NetTickInput &input) {
    InputPacket packet{};
    packet.input = input;
    ENetPacket *net_packet = enet_packet_create(&packet, sizeof(packet), 0);
    enet_peer_send(peer, 0, net_packet);
}

bool NetClient::poll_snapshot(NetSnapshot &out_snapshot) {
    if (!has_snapshot) {
        return false;
    }
    out_snapshot = latest_snapshot;
    has_snapshot = false;
    return true;
}
