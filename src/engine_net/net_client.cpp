#include "engine_net/net_client.hpp"
#include "engine_net_proto/net_packet_codec.hpp"
#if defined(VOXOV_PLATFORM_WEB)
#include "engine_net/web_posix_socket_compat.hpp"
#endif

#include <enet/enet.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdio>
#include <chrono>
#include <vector>

namespace {
constexpr int kMaxEventsPerPump = 8;

uint64_t now_ms() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

bool net_seq_newer(uint32_t incoming, uint32_t last_seen) {
    return incoming != last_seen && static_cast<int32_t>(incoming - last_seen) > 0;
}

template <typename T>
ENetPacket *make_packet(NetMsgType type, uint32_t sequence, const T &payload,
                        enet_uint32 flags, size_t &out_size) {
    std::vector<uint8_t> bytes;
    if (!net_encode_message(type, sequence, payload, bytes)) {
        out_size = 0;
        return nullptr;
    }
    out_size = bytes.size();
    return enet_packet_create(bytes.data(), bytes.size(), flags);
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
    state = NetClientConnectionState::Disconnected;
    next_packet_sequence = 1;
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
    state = NetClientConnectionState::Connecting;
    assigned_player_id = 0;
    has_server_session_info = false;
    server_session_info = NetSessionInfo{};
    target_host = host;
    target_port = port;
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
    state = NetClientConnectionState::Disconnected;
    assigned_player_id = 0;
    has_server_session_info = false;
    server_session_info = NetSessionInfo{};
    has_snapshot = false;
    chunk_updates.clear();
    replicated_players.clear();
    replicated_player_sequences.clear();
    target_host.clear();
    target_port = 0;
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
    has_server_session_info = false;
    server_session_info = NetSessionInfo{};
    has_last_snapshot_sequence = false;
    last_snapshot_sequence = 0;
    state = NetClientConnectionState::Disconnected;
    target_host.clear();
    target_port = 0;
    next_packet_sequence = 1;
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
#if defined(VOXOV_PLATFORM_WEB)
    web_posix_socket_begin_pump();
#endif
    refresh_debug_stats();

    ENetEvent event{};
    int events_processed = 0;
    while (events_processed < kMaxEventsPerPump &&
           enet_host_service(client, &event, 0) > 0) {
        ++events_processed;
        if (event.type == ENET_EVENT_TYPE_CONNECT) {
            connected = true;
            state = NetClientConnectionState::Connected;
            const ENetAddress &addr = event.peer->address;
            spdlog::info(
                "NetClient: connected to {}.{}.{}.{}:{}",
                static_cast<int>((addr.host >> 0) & 0xFF),
                static_cast<int>((addr.host >> 8) & 0xFF),
                static_cast<int>((addr.host >> 16) & 0xFF),
                static_cast<int>((addr.host >> 24) & 0xFF),
                static_cast<int>(addr.port));
            if (has_pending_interest && peer) {
                size_t packet_size = 0;
                ENetPacket *net_packet = make_packet(
                    NetMsgType::ChunkInterest, next_packet_sequence++,
                    pending_interest, ENET_PACKET_FLAG_RELIABLE, packet_size);
                if (net_packet != nullptr) {
                    enet_peer_send(peer,
                                   static_cast<uint8_t>(NetChannel::Reliable),
                                   net_packet);
                    record_tx(packet_size);
                }
                has_pending_interest = false;
            }
            continue;
        }

        if (event.type == ENET_EVENT_TYPE_DISCONNECT) {
            spdlog::warn("NetClient: disconnected from server");
            connected = false;
            state = NetClientConnectionState::Disconnected;
            peer = nullptr;
            assigned_player_id = 0;
            replicated_players.clear();
            replicated_player_sequences.clear();
            has_snapshot = false;
            server_protocol_info = NetProtocolInfo{};
            has_server_session_info = false;
            server_session_info = NetSessionInfo{};
            has_last_snapshot_sequence = false;
            last_snapshot_sequence = 0;
            target_host.clear();
            target_port = 0;
            continue;
        }

        if (event.type == ENET_EVENT_TYPE_RECEIVE) {
            record_rx(event.packet->dataLength);
            bool recognized_message = false;
            NetPacketHeader header{};
            if (net_decode_header(event.packet->data, event.packet->dataLength,
                                  header)) {
                const auto type = static_cast<NetMsgType>(header.type);
                switch (type) {
                case NetMsgType::Snapshot: {
                    NetSnapshot snapshot{};
                    if (net_decode_message(event.packet->data,
                                           event.packet->dataLength, type,
                                           snapshot)) {
                        if (!has_last_snapshot_sequence ||
                            net_seq_newer(snapshot.sequence,
                                          last_snapshot_sequence)) {
                            latest_snapshot = snapshot;
                            has_snapshot = true;
                            last_snapshot_sequence = snapshot.sequence;
                            has_last_snapshot_sequence = true;
                        }
                        recognized_message = true;
                    }
                    break;
                }
                case NetMsgType::ChunkState: {
                    NetChunkState chunk_state{};
                    if (net_decode_message(event.packet->data,
                                           event.packet->dataLength, type,
                                           chunk_state)) {
                        chunk_updates.push_back(chunk_state);
                        recognized_message = true;
                    }
                    break;
                }
                case NetMsgType::AssignPlayer: {
                    NetAssignPlayer assignment{};
                    if (net_decode_message(event.packet->data,
                                           event.packet->dataLength, type,
                                           assignment)) {
                        assigned_player_id = assignment.player_id;
                        recognized_message = assignment.player_id != 0;
                    }
                    break;
                }
                case NetMsgType::PlayerState: {
                    NetPlayerState player_state{};
                    if (net_decode_message(event.packet->data,
                                           event.packet->dataLength, type,
                                           player_state) &&
                        player_state.player_id != 0) {
                        const uint32_t player_id = player_state.player_id;
                        auto seq_it = replicated_player_sequences.find(player_id);
                        if (seq_it == replicated_player_sequences.end() ||
                            net_seq_newer(player_state.sequence,
                                          seq_it->second)) {
                            replicated_players[player_id] = player_state;
                            replicated_player_sequences[player_id] =
                                player_state.sequence;
                        }
                        recognized_message = true;
                    }
                    break;
                }
                case NetMsgType::ProtocolInfo: {
                    NetProtocolInfo protocol{};
                    if (net_decode_message(event.packet->data,
                                           event.packet->dataLength, type,
                                           protocol)) {
                        server_protocol_info = protocol;
                        recognized_message = true;
                    }
                    break;
                }
                case NetMsgType::SessionInfo: {
                    NetSessionInfo session{};
                    if (net_decode_message(event.packet->data,
                                           event.packet->dataLength, type,
                                           session)) {
                        server_session_info = session;
                        has_server_session_info = true;
                        recognized_message = true;
                    }
                    break;
                }
                case NetMsgType::PlayerRemove: {
                    NetPlayerRemove removal{};
                    if (net_decode_message(event.packet->data,
                                           event.packet->dataLength, type,
                                           removal) &&
                        removal.player_id != 0) {
                        replicated_players.erase(removal.player_id);
                        replicated_player_sequences.erase(removal.player_id);
                        recognized_message = true;
                    }
                    break;
                }
                default:
                    break;
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

    size_t packet_size = 0;
    ENetPacket *net_packet = make_packet(NetMsgType::Input,
                                         next_packet_sequence++, input, 0,
                                         packet_size);
    if (net_packet != nullptr) {
        enet_peer_send(peer, static_cast<uint8_t>(NetChannel::Unreliable),
                       net_packet);
        record_tx(packet_size);
    }
}

void NetClient::set_chunk_interest(const NetChunkInterest &interest) {
    pending_interest = interest;
    pending_interest.radius =
        std::min(pending_interest.radius, k_net_max_interest_radius);
    has_pending_interest = true;

    if (!client || !peer || !connected) {
        return;
    }

    size_t packet_size = 0;
    ENetPacket *net_packet = make_packet(
        NetMsgType::ChunkInterest, next_packet_sequence++, pending_interest,
        ENET_PACKET_FLAG_RELIABLE, packet_size);
    if (net_packet != nullptr) {
        enet_peer_send(peer, static_cast<uint8_t>(NetChannel::Reliable),
                       net_packet);
        record_tx(packet_size);
        has_pending_interest = false;
    }
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

bool NetClient::has_session_info() const {
    return has_server_session_info;
}

NetSessionInfo NetClient::session_info() const {
    return server_session_info;
}

bool NetClient::is_connected() const {
    return connected;
}

bool NetClient::is_initialized() const {
    return initialized && client != nullptr;
}

NetClientConnectionState NetClient::connection_state() const {
    return state;
}

const std::string &NetClient::connect_target_host() const {
    return target_host;
}

uint16_t NetClient::connect_target_port() const {
    return target_port;
}

NetDebugStats NetClient::debug_stats() const {
    return debug_counters.snapshot;
}

const std::unordered_map<uint32_t, NetPlayerState> &NetClient::player_states() const {
    return replicated_players;
}
