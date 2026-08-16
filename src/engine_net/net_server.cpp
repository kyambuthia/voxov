#include "engine_net/net_server.hpp"
#include "engine_net_proto/net_packet_codec.hpp"
#include "engine_server/server_session.hpp"
#include "engine_net/net_runtime_shared.hpp"

#include <enet/enet.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
constexpr double kServerSimTickMs = 1000.0 / 60.0;
constexpr uint64_t kSnapshotSendIntervalMs = 33;
// Player presence does not need simulation frequency. Twenty updates per
// second leaves headroom for snapshots and reliable chunk/session traffic at
// the advertised 32-player cap, including browser gateway overhead.
constexpr uint64_t kPlayerBroadcastIntervalMs = 50;
constexpr int kMaxCatchupTicksPerPump = 8;

uint64_t now_ms() {
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
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
} // namespace

NetServer::NetServer() = default;

NetServer::~NetServer() { shutdown(); }

void NetServer::send_session_info(ENetPeer *peer) {
  if (!server || !peer || !session) {
    return;
  }
  const NetSessionInfo info = session->make_session_info(local_only);
  size_t packet_size = 0;
  ENetPacket *out = make_packet(NetMsgType::SessionInfo,
                                next_packet_sequence++, info,
                                ENET_PACKET_FLAG_RELIABLE, packet_size);
  if (out != nullptr) {
    enet_peer_send(peer, static_cast<uint8_t>(NetChannel::Reliable), out);
    record_tx(packet_size);
  }
}

void NetServer::broadcast_session_info() {
  if (!server || !session || session->clients().empty()) {
    return;
  }
  for (auto &[peer_ptr, state] : session->clients()) {
    (void)state;
    send_session_info(peer_ptr);
  }
}

void NetServer::send_chunk_state(ENetPeer *peer, ServerClientState &state,
                                 NetChunkCoord coord, uint32_t version) {
  auto it = state.sent_chunks.find(coord);
  if (it != state.sent_chunks.end() && it->second == version) {
    return;
  }

  const NetChunkState chunk_state =
      net_make_spherical_chunk_state(coord, version);
  size_t packet_size = 0;
  ENetPacket *out = make_packet(NetMsgType::ChunkState,
                                next_packet_sequence++, chunk_state,
                                ENET_PACKET_FLAG_RELIABLE, packet_size);
  if (out != nullptr) {
    enet_peer_send(peer, static_cast<uint8_t>(NetChannel::Reliable), out);
    record_tx(packet_size);
    state.sent_chunks[coord] = version;
  }
}

void NetServer::send_snapshots() {
  if (!server || !session || session->clients().empty()) {
    return;
  }
  const uint64_t now = now_ms();
  if (last_snapshot_send_ms != 0 &&
      (now - last_snapshot_send_ms) < kSnapshotSendIntervalMs) {
    return;
  }
  last_snapshot_send_ms = now;

  for (auto &[peer_ptr, state] : session->clients()) {
    NetSnapshot snapshot{};
    snapshot.player_id = state.player_id;
    snapshot.tick = state.state.tick;
    snapshot.sequence = state.next_snapshot_sequence++;
    snapshot.world = state.state.world;
    snapshot.x = state.state.x;
    snapshot.y = state.state.y;
    snapshot.z = state.state.z;
    snapshot.vx = state.state.vx;
    snapshot.vy = state.state.vy;
    snapshot.vz = state.state.vz;
    size_t packet_size = 0;
    ENetPacket *out = make_packet(NetMsgType::Snapshot,
                                  next_packet_sequence++, snapshot, 0,
                                  packet_size);
    if (out != nullptr) {
      enet_peer_send(peer_ptr, static_cast<uint8_t>(NetChannel::Unreliable),
                     out);
      record_tx(packet_size);
      debug_counters.snapshots_sent_window += 1;
    }
  }
}

void NetServer::broadcast_player_states() {
  if (!session) {
    return;
  }
  const uint64_t now = now_ms();
  if (last_player_broadcast_ms != 0 &&
      (now - last_player_broadcast_ms) < kPlayerBroadcastIntervalMs) {
    return;
  }
  last_player_broadcast_ms = now;

  for (auto &[subject_peer, subject] : session->clients()) {
    (void)subject_peer;
    const uint32_t state_sequence = subject.next_player_state_sequence++;
    for (auto &[observer_peer, observer] : session->clients()) {
      if (!session->should_replicate_player_state(observer, subject)) {
        continue;
      }
      NetPlayerState player_state = subject.state;
      player_state.sequence = state_sequence;
      size_t packet_size = 0;
      ENetPacket *out = make_packet(NetMsgType::PlayerState,
                                    next_packet_sequence++, player_state, 0,
                                    packet_size);
      if (out != nullptr) {
        enet_peer_send(observer_peer,
                       static_cast<uint8_t>(NetChannel::Unreliable), out);
        record_tx(packet_size);
        debug_counters.player_state_broadcasts_window += 1;
      }
    }
  }
}

void NetServer::broadcast_player_remove(uint32_t player_id) {
  if (!server || !session || player_id == 0) {
    return;
  }
  NetPlayerRemove removal{};
  removal.player_id = player_id;
  size_t packet_size = 0;
  ENetPacket *out = make_packet(NetMsgType::PlayerRemove,
                                next_packet_sequence++, removal,
                                ENET_PACKET_FLAG_RELIABLE, packet_size);
  if (out != nullptr) {
    enet_host_broadcast(server, static_cast<uint8_t>(NetChannel::Reliable),
                        out);
    record_tx(packet_size,
              static_cast<uint32_t>(session->clients().size()));
  }
}

void NetServer::record_tx(size_t bytes, uint32_t packet_count) {
  if (packet_count == 0) {
    return;
  }
  debug_counters.tx_packets_total += packet_count;
  debug_counters.tx_bytes_total +=
      static_cast<uint64_t>(bytes) * static_cast<uint64_t>(packet_count);
  debug_counters.tx_packets_window += packet_count;
  debug_counters.tx_bytes_window += static_cast<uint32_t>(bytes * packet_count);
}

void NetServer::record_rx(size_t bytes) {
  debug_counters.rx_packets_total += 1;
  debug_counters.rx_bytes_total += static_cast<uint64_t>(bytes);
  debug_counters.rx_packets_window += 1;
  debug_counters.rx_bytes_window += static_cast<uint32_t>(bytes);
}

void NetServer::refresh_debug_stats() {
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
  debug_counters.snapshot.invalid_packets_total =
      debug_counters.invalid_packets_total;
  debug_counters.snapshot.tx_packets_per_sec = debug_counters.tx_packets_window;
  debug_counters.snapshot.tx_bytes_per_sec = debug_counters.tx_bytes_window;
  debug_counters.snapshot.rx_packets_per_sec = debug_counters.rx_packets_window;
  debug_counters.snapshot.rx_bytes_per_sec = debug_counters.rx_bytes_window;
  debug_counters.snapshot.snapshots_sent_per_sec =
      debug_counters.snapshots_sent_window;
  debug_counters.snapshot.player_state_broadcasts_per_sec =
      debug_counters.player_state_broadcasts_window;

  debug_counters.tx_packets_window = 0;
  debug_counters.tx_bytes_window = 0;
  debug_counters.rx_packets_window = 0;
  debug_counters.rx_bytes_window = 0;
  debug_counters.snapshots_sent_window = 0;
  debug_counters.player_state_broadcasts_window = 0;
  debug_counters.last_rollup_ms = now;
}

bool NetServer::init(uint16_t port, bool loopback_only) {
  if (initialized) {
    return server != nullptr;
  }

  if (enet_initialize() != 0) {
    std::fprintf(stderr, "NetServer: enet_initialize failed\n");
    return false;
  }
  initialized = true;

  ENetAddress address{};
  local_only = loopback_only;
  if (local_only) {
    if (enet_address_set_host(&address, "127.0.0.1") != 0) {
      std::fprintf(stderr, "NetServer: failed to resolve loopback host\n");
      enet_deinitialize();
      initialized = false;
      return false;
    }
  } else {
    address.host = ENET_HOST_ANY;
  }
  address.port = port;
  server = enet_host_create(&address, 32, 2, 0, 0);
  if (!server) {
    std::fprintf(stderr, "NetServer: enet_host_create failed on port %u\n",
                 port);
    enet_deinitialize();
    initialized = false;
    return false;
  }
  last_player_broadcast_ms = 0;
  next_packet_sequence = 1;
  last_pump_ms = 0;
  sim_accumulator_ms = 0.0;
  last_snapshot_send_ms = 0;
  session = std::make_unique<ServerSession>();
  debug_counters = DebugCounters{};
  refresh_debug_stats();
  return true;
}

void NetServer::shutdown() {
  if (session) {
    session->reset();
  }
  last_pump_ms = 0;
  sim_accumulator_ms = 0.0;
  last_snapshot_send_ms = 0;
  last_player_broadcast_ms = 0;
  next_packet_sequence = 1;
  if (server) {
    enet_host_destroy(server);
    server = nullptr;
  }
  if (initialized) {
    enet_deinitialize();
    initialized = false;
  }
  session.reset();
  debug_counters = DebugCounters{};
}

void NetServer::pump() {
  if (!server) {
    return;
  }
  refresh_debug_stats();

  const uint64_t pump_now = now_ms();
  if (last_pump_ms == 0) {
    last_pump_ms = pump_now;
  }
  uint64_t delta_ms = pump_now - last_pump_ms;
  last_pump_ms = pump_now;
  if (delta_ms > 250) {
    delta_ms = 250;
  }
  sim_accumulator_ms += static_cast<double>(delta_ms);

  ENetEvent event{};
  while (enet_host_service(server, &event, 0) > 0) {
    switch (event.type) {
    case ENET_EVENT_TYPE_CONNECT: {
      if (local_only) {
        char ip_buffer[64]{};
        if (enet_address_get_host_ip(&event.peer->address, ip_buffer,
                                     sizeof(ip_buffer)) != 0) {
          std::snprintf(ip_buffer, sizeof(ip_buffer), "%s", "unknown");
        }
        const bool localhost = std::strcmp(ip_buffer, "127.0.0.1") == 0 ||
                               std::strcmp(ip_buffer, "::1") == 0;
        if (!localhost) {
          spdlog::warn(
              "NetServer: rejected non-local peer {} in loopback-only mode",
              ip_buffer);
          enet_peer_disconnect_now(event.peer, 0);
          break;
        }
      }
      if (!session) {
        break;
      }
      ServerClientState &state = session->connect_client(event.peer);
      spdlog::info(
          "NetServer: client connected, assigned player_id={}, clients={}",
          state.player_id, session->clients().size());

      NetAssignPlayer assignment{};
      assignment.player_id = state.player_id;
      size_t assignment_size = 0;
      ENetPacket *out = make_packet(NetMsgType::AssignPlayer,
                                    next_packet_sequence++, assignment,
                                    ENET_PACKET_FLAG_RELIABLE,
                                    assignment_size);
      if (out != nullptr) {
        enet_peer_send(event.peer,
                       static_cast<uint8_t>(NetChannel::Reliable), out);
        record_tx(assignment_size);
      }

      NetProtocolInfo protocol{};
      protocol.protocol_version = k_net_protocol_version;
      protocol.feature_flags =
          net_feature(NetFeatureFlags::InterestFilteredReplication) |
          net_feature(NetFeatureFlags::ChunkStreaming);
      protocol.server_tick_hz = 60;
      size_t protocol_size = 0;
      ENetPacket *proto_packet = make_packet(
          NetMsgType::ProtocolInfo, next_packet_sequence++, protocol,
          ENET_PACKET_FLAG_RELIABLE, protocol_size);
      if (proto_packet != nullptr) {
        enet_peer_send(event.peer,
                       static_cast<uint8_t>(NetChannel::Reliable),
                       proto_packet);
        record_tx(protocol_size);
      }
      broadcast_session_info();
      break;
    }
    case ENET_EVENT_TYPE_DISCONNECT: {
      if (session) {
        const std::optional<uint32_t> removed_player_id =
            session->disconnect_client(event.peer);
        broadcast_session_info();
        if (removed_player_id.has_value()) {
          broadcast_player_remove(*removed_player_id);
        }
      }
      spdlog::info("NetServer: client disconnected, clients={}",
                   session ? session->clients().size() : 0u);
      break;
    }
    case ENET_EVENT_TYPE_RECEIVE: {
      record_rx(event.packet->dataLength);
      bool recognized_message = false;
      if (!session) {
        debug_counters.invalid_packets_total += 1;
        enet_packet_destroy(event.packet);
        break;
      }
      ServerClientState *state = session->find_client(event.peer);
      if (!state) {
        debug_counters.invalid_packets_total += 1;
        enet_packet_destroy(event.packet);
        break;
      }

      NetPacketHeader header{};
      if (net_decode_header(event.packet->data, event.packet->dataLength,
                            header)) {
        const auto type = static_cast<NetMsgType>(header.type);
        switch (type) {
        case NetMsgType::Input: {
          NetTickInput input{};
          recognized_message =
              net_decode_message(event.packet->data,
                                 event.packet->dataLength, type, input) &&
              session->accept_input(*state, input);
          break;
        }
        case NetMsgType::ChunkInterest: {
          NetChunkInterest interest{};
          if (net_decode_message(event.packet->data,
                                 event.packet->dataLength, type, interest) &&
              session->accept_interest(*state, interest, now_ms())) {
            recognized_message = true;
            for (int dz = -interest.radius; dz <= interest.radius; ++dz) {
              for (int dx = -interest.radius; dx <= interest.radius; ++dx) {
                NetChunkCoord coord{};
                coord.body_id = interest.body_id;
                coord.face = interest.face;
                coord.shell = interest.shell;
                coord.x = static_cast<int16_t>(interest.center_x + dx);
                coord.y = interest.center_y;
                coord.z = static_cast<int16_t>(interest.center_z + dz);
                send_chunk_state(event.peer, *state, coord, 1);
              }
            }
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
      break;
    }
    default:
      break;
    }
  }

  int catchup_ticks = 0;
  while (sim_accumulator_ms >= kServerSimTickMs &&
         catchup_ticks < kMaxCatchupTicksPerPump) {
    if (session) {
      session->simulate_fixed_tick();
    }
    sim_accumulator_ms -= kServerSimTickMs;
    ++catchup_ticks;
  }
  if (catchup_ticks == kMaxCatchupTicksPerPump &&
      sim_accumulator_ms >= kServerSimTickMs) {
    sim_accumulator_ms = std::fmod(sim_accumulator_ms, kServerSimTickMs);
    spdlog::warn(
        "NetServer: sim catch-up capped ({} ticks), dropping accumulated time",
        kMaxCatchupTicksPerPump);
  }

  send_snapshots();
  broadcast_player_states();
}

uint16_t NetServer::bound_port() const {
  if (!server) {
    return 0;
  }
  return server->address.port;
}

NetDebugStats NetServer::debug_stats() const { return debug_counters.snapshot; }
