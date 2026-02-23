#include "engine_net/net_client.hpp"

#include <enet/enet.h>
#include <spdlog/spdlog.h>

#include <cstring>
#include <cstdio>
#include <chrono>

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

struct PlayerRemovePacket {
    NetMsgType type = NetMsgType::PlayerRemove;
    NetPlayerRemove payload{};
};
#pragma pack(pop)

uint64_t now_ms() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}
}

void NetClient::record_tx(size_t bytes) {
    debug_counters.tx_packets_total += 1;
    debug_counters.tx_bytes_total += static_cast<uint64_t>(bytes);
    debug_counters.tx_packets_window += 1;
    debug_counters.tx_bytes_window += static_cast<uint32_t>(bytes);
}

void NetClient::record_rx(size_t bytes) {
    debug_counters.rx_packets_total += 1;
    debug_counters.rx_bytes_total += static_cast<uint64_t>(bytes);
    debug_counters.rx_packets_window += 1;
    debug_counters.rx_bytes_window += static_cast<uint32_t>(bytes);
}

void NetClient::refresh_debug_stats() {
    const uint64_t now = now_ms();
    if (debug_counters.last_rollup_ms == 0) {
        debug_counters.last_rollup_ms = now;
    }
    if ((now - debug_counters.last_rollup_ms) < 1000) {
        return;
    }

    debug_counters.snapshot.tx_packets_total = debug_counters.tx_packets_total;
    debug_counters.snapshot.tx_bytes_total = debug_counters.tx_bytes_total;
    debug_counters.snapshot.rx_packets_total = debug_counters.rx_packets_total;
    debug_counters.snapshot.rx_bytes_total = debug_counters.rx_bytes_total;
    debug_counters.snapshot.invalid_packets_total = debug_counters.invalid_packets_total;
    debug_counters.snapshot.tx_packets_per_sec = debug_counters.tx_packets_window;
    debug_counters.snapshot.tx_bytes_per_sec = debug_counters.tx_bytes_window;
    debug_counters.snapshot.rx_packets_per_sec = debug_counters.rx_packets_window;
    debug_counters.snapshot.rx_bytes_per_sec = debug_counters.rx_bytes_window;
    debug_counters.snapshot.snapshots_sent_per_sec = 0;
    debug_counters.snapshot.player_state_broadcasts_per_sec = 0;

    debug_counters.tx_packets_window = 0;
    debug_counters.tx_bytes_window = 0;
    debug_counters.rx_packets_window = 0;
    debug_counters.rx_bytes_window = 0;
    debug_counters.last_rollup_ms = now;
}

bool NetClient::init() {
    if (initialized) {
        return true;
    }

    if (enet_initialize() != 0) {
        std::fprintf(stderr, "NetClient: enet_initialize failed\n");
        return false;
    }
    client = enet_host_create(nullptr, 1, 2, 0, 0);
    if (!client) {
        std::fprintf(stderr, "NetClient: enet_host_create failed\n");
        enet_deinitialize();
        return false;
    }
    initialized = true;
    debug_counters = DebugCounters{};
    refresh_debug_stats();
    return true;
}

bool NetClient::connect(const char *host, uint16_t port) {
    if (!initialized || !client || !host) {
        return false;
    }
    disconnect();

    ENetAddress address{};
    if (enet_address_set_host(&address, host) != 0) {
        std::fprintf(stderr, "NetClient: failed to resolve host '%s'\n", host);
        return false;
    }
    address.port = port;
    peer = enet_host_connect(client, &address, 2, 0);
    if (!peer) {
        std::fprintf(stderr, "NetClient: enet_host_connect failed\n");
        return false;
    }
    connected = false;
    assigned_player_id = 0;
    spdlog::info("NetClient: connecting to {}:{}", host, port);
    return true;
}

void NetClient::disconnect() {
    if (peer) {
        enet_peer_disconnect(peer, 0);
        enet_host_flush(client);
        peer = nullptr;
    }
    connected = false;
}

void NetClient::shutdown() {
    chunk_updates.clear();
    replicated_players.clear();
    assigned_player_id = 0;
    connected = false;
    has_pending_interest = false;
    if (client) {
        enet_host_destroy(client);
        client = nullptr;
    }
    if (initialized) {
        enet_deinitialize();
        initialized = false;
    }
    debug_counters = DebugCounters{};
}

void NetClient::pump() {
    if (!client) {
        return;
    }
    refresh_debug_stats();

    ENetEvent event{};
    while (enet_host_service(client, &event, 0) > 0) {
        if (event.type == ENET_EVENT_TYPE_CONNECT) {
            connected = true;
            const ENetAddress &addr = event.peer->address;
            spdlog::info(
                "NetClient: connected to {}.{}.{}.{}:{}",
                static_cast<int>((addr.host >> 0) & 0xFF),
                static_cast<int>((addr.host >> 8) & 0xFF),
                static_cast<int>((addr.host >> 16) & 0xFF),
                static_cast<int>((addr.host >> 24) & 0xFF),
                static_cast<int>(addr.port));
            if (has_pending_interest && peer) {
                ChunkInterestPacket packet{};
                packet.interest = pending_interest;
                ENetPacket *net_packet = enet_packet_create(&packet, sizeof(packet), ENET_PACKET_FLAG_RELIABLE);
                enet_peer_send(peer, static_cast<uint8_t>(NetChannel::Reliable), net_packet);
                record_tx(sizeof(packet));
                has_pending_interest = false;
            }
            continue;
        }

        if (event.type == ENET_EVENT_TYPE_DISCONNECT) {
            spdlog::warn("NetClient: disconnected from server");
            connected = false;
            peer = nullptr;
            assigned_player_id = 0;
            replicated_players.clear();
            has_snapshot = false;
            continue;
        }

        if (event.type == ENET_EVENT_TYPE_RECEIVE) {
            record_rx(event.packet->dataLength);
            bool recognized_message = false;
            if (event.packet->dataLength >= sizeof(SnapshotPacket)) {
                SnapshotPacket packet{};
                std::memcpy(&packet, event.packet->data, sizeof(packet));
                if (packet.type == NetMsgType::Snapshot) {
                    latest_snapshot = packet.snapshot;
                    has_snapshot = true;
                    recognized_message = true;
                }
            }

            if (event.packet->dataLength >= sizeof(ChunkStatePacket)) {
                ChunkStatePacket packet{};
                std::memcpy(&packet, event.packet->data, sizeof(packet));
                if (packet.type == NetMsgType::ChunkState) {
                    chunk_updates.push_back(packet.state);
                    recognized_message = true;
                }
            }

            if (event.packet->dataLength >= sizeof(AssignPlayerPacket)) {
                AssignPlayerPacket packet{};
                std::memcpy(&packet, event.packet->data, sizeof(packet));
                if (packet.type == NetMsgType::AssignPlayer) {
                    assigned_player_id = packet.payload.player_id;
                    recognized_message = true;
                }
            }

            if (event.packet->dataLength >= sizeof(PlayerStatePacket)) {
                PlayerStatePacket packet{};
                std::memcpy(&packet, event.packet->data, sizeof(packet));
                if (packet.type == NetMsgType::PlayerState) {
                    replicated_players[packet.state.player_id] = packet.state;
                    recognized_message = true;
                }
            }

            if (event.packet->dataLength >= sizeof(PlayerRemovePacket)) {
                PlayerRemovePacket packet{};
                std::memcpy(&packet, event.packet->data, sizeof(packet));
                if (packet.type == NetMsgType::PlayerRemove) {
                    replicated_players.erase(packet.payload.player_id);
                    recognized_message = true;
                }
            }
            if (!recognized_message) {
                debug_counters.invalid_packets_total += 1;
            }

            enet_packet_destroy(event.packet);
        }
    }
}

void NetClient::send_input(const NetTickInput &input) {
    if (!client || !peer || !connected) {
        return;
    }

    InputPacket packet{};
    packet.input = input;
    ENetPacket *net_packet = enet_packet_create(&packet, sizeof(packet), 0);
    enet_peer_send(peer, static_cast<uint8_t>(NetChannel::Unreliable), net_packet);
    record_tx(sizeof(packet));
}

void NetClient::set_chunk_interest(const NetChunkInterest &interest) {
    pending_interest = interest;
    has_pending_interest = true;

    if (!client || !peer || !connected) {
        return;
    }

    ChunkInterestPacket packet{};
    packet.interest = interest;
    ENetPacket *net_packet = enet_packet_create(&packet, sizeof(packet), ENET_PACKET_FLAG_RELIABLE);
    enet_peer_send(peer, static_cast<uint8_t>(NetChannel::Reliable), net_packet);
    record_tx(sizeof(packet));
    has_pending_interest = false;
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

uint32_t NetClient::local_player_id() const {
    return assigned_player_id;
}

bool NetClient::is_connected() const {
    return connected;
}

bool NetClient::is_initialized() const {
    return initialized && client != nullptr;
}

NetDebugStats NetClient::debug_stats() const {
    return debug_counters.snapshot;
}

const std::unordered_map<uint32_t, NetPlayerState> &NetClient::player_states() const {
    return replicated_players;
}
