#include "engine_net/net_server.hpp"

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

int32_t NetServer::chunk_key(NetChunkCoord coord) const {
    return (static_cast<int32_t>(coord.x) << 16) ^ static_cast<uint16_t>(coord.z);
}

void NetServer::send_chunk_state(ENetPeer *peer, ClientState &state, NetChunkCoord coord, uint32_t version) {
    const int32_t key = chunk_key(coord);
    auto it = state.sent_chunks.find(key);
    if (it != state.sent_chunks.end() && it->second == version) {
        return;
    }

    ChunkStatePacket packet{};
    packet.state.coord = coord;
    packet.state.version = version;

    ENetPacket *out = enet_packet_create(&packet, sizeof(packet), ENET_PACKET_FLAG_RELIABLE);
    enet_peer_send(peer, static_cast<uint8_t>(NetChannel::Reliable), out);
    state.sent_chunks[key] = version;
}

void NetServer::init(uint16_t port) {
    enet_initialize();

    ENetAddress address{};
    address.host = ENET_HOST_ANY;
    address.port = port;
    server = enet_host_create(&address, 32, 2, 0, 0);
}

void NetServer::shutdown() {
    clients.clear();
    if (server) {
        enet_host_destroy(server);
        server = nullptr;
    }
    enet_deinitialize();
}

void NetServer::pump() {
    if (!server) {
        return;
    }

    ENetEvent event{};
    while (enet_host_service(server, &event, 0) > 0) {
        switch (event.type) {
        case ENET_EVENT_TYPE_CONNECT:
            clients[event.peer] = ClientState{};
            break;
        case ENET_EVENT_TYPE_DISCONNECT:
            clients.erase(event.peer);
            break;
        case ENET_EVENT_TYPE_RECEIVE: {
            ClientState &state = clients[event.peer];

            if (event.packet->dataLength >= sizeof(InputPacket)) {
                InputPacket packet{};
                std::memcpy(&packet, event.packet->data, sizeof(InputPacket));
                if (packet.type == NetMsgType::Input) {
                    state.sim_x += packet.input.move_x * 0.05f;

                    SnapshotPacket snap{};
                    snap.snapshot.tick = packet.input.tick;
                    snap.snapshot.x = state.sim_x;
                    ENetPacket *out = enet_packet_create(&snap, sizeof(snap), 0);
                    enet_peer_send(event.peer, static_cast<uint8_t>(NetChannel::Unreliable), out);
                }
            }

            if (event.packet->dataLength >= sizeof(ChunkInterestPacket)) {
                ChunkInterestPacket interest_packet{};
                std::memcpy(&interest_packet, event.packet->data, sizeof(ChunkInterestPacket));
                if (interest_packet.type == NetMsgType::ChunkInterest) {
                    state.interest = interest_packet.interest;
                    for (int dz = -state.interest.radius; dz <= state.interest.radius; ++dz) {
                        for (int dx = -state.interest.radius; dx <= state.interest.radius; ++dx) {
                            NetChunkCoord coord{};
                            coord.x = static_cast<int16_t>(state.interest.center_x + dx);
                            coord.z = static_cast<int16_t>(state.interest.center_z + dz);
                            send_chunk_state(event.peer, state, coord, 1);
                        }
                    }
                }
            }

            enet_packet_destroy(event.packet);
            break;
        }
        default:
            break;
        }
    }
}
