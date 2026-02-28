#include "engine_net/net_client.hpp"

#include <enet/enet.h>
#include <spdlog/spdlog.h>

#include <cstring>
#include <cstdio>
#include <chrono>

namespace {
#pragma pack(push, 1)
struct InputPacket {
    NetPacketHeader header{};
    NetTickInput input{};
};

struct SnapshotPacket {
    NetPacketHeader header{};
    NetSnapshot snapshot{};
};

struct ChunkInterestPacket {
    NetPacketHeader header{};
    NetChunkInterest interest{};
};

struct ChunkStatePacket {
    NetPacketHeader header{};
    NetChunkState state{};
};

struct AssignPlayerPacket {
    NetPacketHeader header{};
    NetAssignPlayer payload{};
};

struct ProtocolInfoPacket {
    NetPacketHeader header{};
    NetProtocolInfo payload{};
};

struct PlayerStatePacket {
    NetPacketHeader header{};
    NetPlayerState state{};
};

struct PlayerRemovePacket {
    NetPacketHeader header{};
    NetPlayerRemove payload{};
};
#pragma pack(pop)

uint64_t now_ms() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

bool net_seq_newer(uint32_t incoming, uint32_t last_seen) {
    return incoming != last_seen && static_cast<int32_t>(incoming - last_seen) > 0;
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
    replicated_player_sequences.clear();
    assigned_player_id = 0;
    connected = false;
    has_pending_interest = false;
    has_snapshot = false;
    server_protocol_info = NetProtocolInfo{};
    has_last_snapshot_sequence = false;
    last_snapshot_sequence = 0;
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
                packet.header = net_make_header(NetMsgType::ChunkInterest, static_cast<uint8_t>(sizeof(packet.interest)));
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
            replicated_player_sequences.clear();
            has_snapshot = false;
            server_protocol_info = NetProtocolInfo{};
            has_last_snapshot_sequence = false;
            last_snapshot_sequence = 0;
            continue;
        }

        if (event.type == ENET_EVENT_TYPE_RECEIVE) {
            record_rx(event.packet->dataLength);
            bool recognized_message = false;
            if (event.packet->dataLength >= sizeof(NetPacketHeader)) {
                NetPacketHeader header{};
                std::memcpy(&header, event.packet->data, sizeof(header));
                if (net_header_basic_valid(header) &&
                    event.packet->dataLength == (sizeof(NetPacketHeader) + header.payload_size)) {
                    switch (static_cast<NetMsgType>(header.type)) {
                    case NetMsgType::Snapshot:
                        if (header.payload_size == sizeof(NetSnapshot) &&
                            event.packet->dataLength == sizeof(SnapshotPacket)) {
                            SnapshotPacket packet{};
                            std::memcpy(&packet, event.packet->data, sizeof(packet));
                            if (!has_last_snapshot_sequence ||
                                net_seq_newer(packet.snapshot.sequence, last_snapshot_sequence)) {
                                latest_snapshot = packet.snapshot;
                                has_snapshot = true;
                                last_snapshot_sequence = packet.snapshot.sequence;
                                has_last_snapshot_sequence = true;
                            }
                            recognized_message = true;
                        }
                        break;
                    case NetMsgType::ChunkState:
                        if (header.payload_size == sizeof(NetChunkState) &&
                            event.packet->dataLength == sizeof(ChunkStatePacket)) {
                            ChunkStatePacket packet{};
                            std::memcpy(&packet, event.packet->data, sizeof(packet));
                            chunk_updates.push_back(packet.state);
                            recognized_message = true;
                        }
                        break;
                    case NetMsgType::AssignPlayer:
                        if (header.payload_size == sizeof(NetAssignPlayer) &&
                            event.packet->dataLength == sizeof(AssignPlayerPacket)) {
                            AssignPlayerPacket packet{};
                            std::memcpy(&packet, event.packet->data, sizeof(packet));
                            assigned_player_id = packet.payload.player_id;
                            recognized_message = true;
                        }
                        break;
                    case NetMsgType::PlayerState:
                        if (header.payload_size == sizeof(NetPlayerState) &&
                            event.packet->dataLength == sizeof(PlayerStatePacket)) {
                            PlayerStatePacket packet{};
                            std::memcpy(&packet, event.packet->data, sizeof(packet));
                            const uint32_t player_id = packet.state.player_id;
                            if (player_id != 0) {
                                auto seq_it = replicated_player_sequences.find(player_id);
                                if (seq_it == replicated_player_sequences.end() ||
                                    net_seq_newer(packet.state.sequence, seq_it->second)) {
                                    replicated_players[player_id] = packet.state;
                                    replicated_player_sequences[player_id] = packet.state.sequence;
                                }
                                recognized_message = true;
                            }
                        }
                        break;
                    case NetMsgType::ProtocolInfo:
                        if (header.payload_size == sizeof(NetProtocolInfo) &&
                            event.packet->dataLength == sizeof(ProtocolInfoPacket)) {
                            ProtocolInfoPacket packet{};
                            std::memcpy(&packet, event.packet->data, sizeof(packet));
                            server_protocol_info = packet.payload;
                            recognized_message = true;
                        }
                        break;
                    case NetMsgType::PlayerRemove:
                        if (header.payload_size == sizeof(NetPlayerRemove) &&
                            event.packet->dataLength == sizeof(PlayerRemovePacket)) {
                            PlayerRemovePacket packet{};
                            std::memcpy(&packet, event.packet->data, sizeof(packet));
                            replicated_players.erase(packet.payload.player_id);
                            replicated_player_sequences.erase(packet.payload.player_id);
                            recognized_message = true;
                        }
                        break;
                    default:
                        break;
                    }
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
    packet.header = net_make_header(NetMsgType::Input, static_cast<uint8_t>(sizeof(packet.input)));
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
    packet.header = net_make_header(NetMsgType::ChunkInterest, static_cast<uint8_t>(sizeof(packet.interest)));
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

NetProtocolInfo NetClient::protocol_info() const {
    return server_protocol_info;
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
