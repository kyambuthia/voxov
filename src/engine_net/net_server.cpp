#include "engine_net/net_server.hpp"

#include <enet/enet.h>
#include <spdlog/spdlog.h>

#include <cstring>
#include <cstdio>

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

struct AssignPlayerPacket {
    NetMsgType type = NetMsgType::AssignPlayer;
    NetAssignPlayer payload{};
};

struct PlayerStatePacket {
    NetMsgType type = NetMsgType::PlayerState;
    NetPlayerState state{};
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

void NetServer::broadcast_player_states() {
    for (const auto &[peer_ptr, state] : clients) {
        (void)peer_ptr;
        PlayerStatePacket packet{};
        packet.state = state.state;

        ENetPacket *out = enet_packet_create(&packet, sizeof(packet), 0);
        enet_host_broadcast(server, static_cast<uint8_t>(NetChannel::Unreliable), out);
    }
}

void NetServer::init(uint16_t port) {
    if (initialized) {
        return;
    }

    if (enet_initialize() != 0) {
        std::fprintf(stderr, "NetServer: enet_initialize failed\n");
        return;
    }
    initialized = true;

    ENetAddress address{};
    address.host = ENET_HOST_ANY;
    address.port = port;
    server = enet_host_create(&address, 32, 2, 0, 0);
    if (!server) {
        std::fprintf(stderr, "NetServer: enet_host_create failed on port %u\n", port);
        enet_deinitialize();
        initialized = false;
    }
}

void NetServer::shutdown() {
    clients.clear();
    if (server) {
        enet_host_destroy(server);
        server = nullptr;
    }
    if (initialized) {
        enet_deinitialize();
        initialized = false;
    }
}

void NetServer::pump() {
    if (!server) {
        return;
    }

    ENetEvent event{};
    while (enet_host_service(server, &event, 0) > 0) {
        switch (event.type) {
        case ENET_EVENT_TYPE_CONNECT: {
            ClientState state{};
            state.player_id = next_player_id++;
            state.state.player_id = state.player_id;
            state.state.x = 8.0f + static_cast<float>((state.player_id % 3) * 2);
            state.state.y = 8.0f;
            state.state.z = 8.0f;
            clients[event.peer] = state;
            spdlog::info("NetServer: client connected, assigned player_id={}, clients={}", state.player_id, clients.size());

            AssignPlayerPacket assign{};
            assign.payload.player_id = state.player_id;
            ENetPacket *out = enet_packet_create(&assign, sizeof(assign), ENET_PACKET_FLAG_RELIABLE);
            enet_peer_send(event.peer, static_cast<uint8_t>(NetChannel::Reliable), out);
            break;
        }
        case ENET_EVENT_TYPE_DISCONNECT:
            clients.erase(event.peer);
            spdlog::info("NetServer: client disconnected, clients={}", clients.size());
            break;
        case ENET_EVENT_TYPE_RECEIVE: {
            auto it = clients.find(event.peer);
            if (it == clients.end()) {
                enet_packet_destroy(event.packet);
                break;
            }

            ClientState &state = it->second;

            if (event.packet->dataLength >= sizeof(InputPacket)) {
                InputPacket packet{};
                std::memcpy(&packet, event.packet->data, sizeof(packet));
                if (packet.type == NetMsgType::Input) {
                    state.last_input = packet.input;

                    const float speed = 4.5f;
                    state.state.x += packet.input.move_x * speed * (1.0f / 60.0f);
                    state.state.z += packet.input.move_y * speed * (1.0f / 60.0f);
                    state.state.vx = packet.input.move_x * speed;
                    state.state.vz = packet.input.move_y * speed;

                    SnapshotPacket snap{};
                    snap.snapshot.player_id = state.player_id;
                    snap.snapshot.tick = packet.input.tick;
                    snap.snapshot.x = state.state.x;
                    snap.snapshot.y = state.state.y;
                    snap.snapshot.z = state.state.z;
                    snap.snapshot.vx = state.state.vx;
                    snap.snapshot.vy = state.state.vy;
                    snap.snapshot.vz = state.state.vz;
                    ENetPacket *out = enet_packet_create(&snap, sizeof(snap), 0);
                    enet_peer_send(event.peer, static_cast<uint8_t>(NetChannel::Unreliable), out);
                }
            }

            if (event.packet->dataLength >= sizeof(ChunkInterestPacket)) {
                ChunkInterestPacket interest_packet{};
                std::memcpy(&interest_packet, event.packet->data, sizeof(interest_packet));
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

    broadcast_player_states();
}
