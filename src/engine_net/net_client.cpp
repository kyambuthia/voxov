#include "engine_net/net_client.hpp"

#include <enet/enet.h>
#include <cstring>

namespace {
#pragma pack(push, 1)
struct InputPacket {
    NetMsgType type = NetMsgType::Input;
    NetTickInput input{};
};

struct SnapshotPacket {
    NetMsgType type = NetMsgType::Snapshot;
    NetSnapshot snapshot{};
};

struct ChunkInterestPacket {
    NetMsgType type = NetMsgType::ChunkInterest;
    NetChunkInterest interest{};
};

struct ChunkStatePacket {
    NetMsgType type = NetMsgType::ChunkState;
    NetChunkState state{};
};
#pragma pack(pop)
}

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
    chunk_updates.clear();
    if (client) {
        enet_host_destroy(client);
        client = nullptr;
    }
    enet_deinitialize();
}

void NetClient::pump() {
    if (!client) {
        return;
    }

    ENetEvent event{};
    while (enet_host_service(client, &event, 0) > 0) {
        if (event.type == ENET_EVENT_TYPE_RECEIVE) {
            if (event.packet->dataLength >= sizeof(SnapshotPacket)) {
                SnapshotPacket packet{};
                std::memcpy(&packet, event.packet->data, sizeof(SnapshotPacket));
                if (packet.type == NetMsgType::Snapshot) {
                    latest_snapshot = packet.snapshot;
                    has_snapshot = true;
                }
            }

            if (event.packet->dataLength >= sizeof(ChunkStatePacket)) {
                ChunkStatePacket packet{};
                std::memcpy(&packet, event.packet->data, sizeof(ChunkStatePacket));
                if (packet.type == NetMsgType::ChunkState) {
                    chunk_updates.push_back(packet.state);
                }
            }

            enet_packet_destroy(event.packet);
        }
    }
}

void NetClient::send_input(const NetTickInput &input) {
    if (!client || !peer) {
        return;
    }

    InputPacket packet{};
    packet.input = input;
    ENetPacket *net_packet = enet_packet_create(&packet, sizeof(packet), 0);
    enet_peer_send(peer, static_cast<uint8_t>(NetChannel::Unreliable), net_packet);
}

void NetClient::set_chunk_interest(const NetChunkInterest &interest) {
    if (!client || !peer) {
        return;
    }

    ChunkInterestPacket packet{};
    packet.interest = interest;
    ENetPacket *net_packet = enet_packet_create(&packet, sizeof(packet), ENET_PACKET_FLAG_RELIABLE);
    enet_peer_send(peer, static_cast<uint8_t>(NetChannel::Reliable), net_packet);
}

bool NetClient::poll_snapshot(NetSnapshot &out_snapshot) {
    if (!has_snapshot) {
        return false;
    }

    out_snapshot = latest_snapshot;
    has_snapshot = false;
    return true;
}

bool NetClient::poll_chunk_state(NetChunkState &out_state) {
    if (chunk_updates.empty()) {
        return false;
    }

    out_state = chunk_updates.back();
    chunk_updates.pop_back();
    return true;
}
