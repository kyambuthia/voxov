#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>

enum class NetChannel : uint8_t { Reliable = 0, Unreliable = 1 };

struct NetTickInput {
  uint32_t tick = 0;
  uint32_t body_id = 1;
  float move_x = 0.0f;
  float move_y = 0.0f;
  float camera_yaw_deg = 180.0f;
  uint8_t action_flags = 0;
};

enum class NetInputFlags : uint8_t {
  JumpHeld = 1u << 0u,
  JumpPressed = 1u << 1u,
  SprintHeld = 1u << 2u,
  CrouchHeld = 1u << 3u
};

constexpr uint8_t net_flag(NetInputFlags flag) {
  return static_cast<uint8_t>(flag);
}

constexpr bool net_flag_set(uint8_t flags, NetInputFlags flag) {
  return (flags & net_flag(flag)) != 0;
}

enum class NetCoordinateFrame : uint8_t {
  BodyLocal = 0,
  Orbital = 1,
  System = 2,
};

struct NetWorldAddress {
  uint64_t system_id = 1;
  uint32_t body_id = 1;
  uint8_t frame = static_cast<uint8_t>(NetCoordinateFrame::BodyLocal);
  uint8_t reserved[3]{};
};

struct NetSnapshot {
  uint32_t player_id = 0;
  uint32_t tick = 0;
  uint32_t sequence = 0;
  NetWorldAddress world{};
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
  float vx = 0.0f;
  float vy = 0.0f;
  float vz = 0.0f;
};

enum class NetMsgType : uint8_t {
  Input = 1,
  Snapshot = 2,
  ChunkInterest = 3,
  ChunkState = 4,
  AssignPlayer = 5,
  PlayerState = 6,
  PlayerRemove = 7,
  ProtocolInfo = 8,
  SessionInfo = 9
};

constexpr uint32_t k_net_packet_magic = 0x564F5832u; // "VOX2"
constexpr uint16_t k_net_protocol_version = 7u;
constexpr uint16_t k_net_max_payload_bytes = 2048u;
constexpr uint8_t k_net_max_interest_radius = 8u;
constexpr size_t k_net_wire_header_size = 14u;

enum class NetFeatureFlags : uint16_t {
  None = 0,
  InterestFilteredReplication = 1u << 0u,
  ChunkStreaming = 1u << 1u
};

constexpr uint16_t net_feature(NetFeatureFlags feature) {
  return static_cast<uint16_t>(feature);
}

constexpr bool net_feature_set(uint16_t flags, NetFeatureFlags feature) {
  return (flags & net_feature(feature)) != 0;
}

enum class NetSessionFlags : uint8_t {
  None = 0,
  LoopbackOnly = 1u << 0u,
  LanAdvertised = 1u << 1u
};

constexpr uint8_t net_session_flag(NetSessionFlags flag) {
  return static_cast<uint8_t>(flag);
}

constexpr bool net_session_flag_set(uint8_t flags, NetSessionFlags flag) {
  return (flags & net_session_flag(flag)) != 0;
}

struct NetPacketHeader {
  uint32_t magic = k_net_packet_magic;
  uint16_t version = k_net_protocol_version;
  uint8_t type = 0;
  uint8_t flags = 0;
  uint32_t sequence = 0;
  uint16_t payload_size = 0;
};

inline NetPacketHeader net_make_header(NetMsgType type, uint16_t payload_size,
                                       uint32_t sequence = 0,
                                       uint8_t flags = 0) {
  NetPacketHeader header{};
  header.type = static_cast<uint8_t>(type);
  header.flags = flags;
  header.sequence = sequence;
  header.payload_size = payload_size;
  return header;
}

inline bool net_header_basic_valid(const NetPacketHeader &header) {
  return header.magic == k_net_packet_magic &&
         header.version == k_net_protocol_version && header.type != 0 &&
         header.payload_size <= k_net_max_payload_bytes;
}

struct NetAssignPlayer {
  uint32_t player_id = 0;
};

struct NetProtocolInfo {
  uint16_t protocol_version = k_net_protocol_version;
  uint16_t feature_flags = 0;
  uint16_t server_tick_hz = 60;
  uint16_t reserved = 0;
};

struct NetSessionInfo {
  char server_name[48]{};
  uint64_t world_seed = 0;
  uint16_t current_players = 0;
  uint16_t max_players = 0;
  uint8_t flags = 0;
  uint8_t reserved[7]{};
};

struct NetPlayerState {
  uint32_t player_id = 0;
  uint32_t tick = 0;
  uint32_t sequence = 0;
  NetWorldAddress world{};
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
  float vx = 0.0f;
  float vy = 0.0f;
  float vz = 0.0f;
  uint8_t anim_state = 0;
  float anim_phase = 0.0f;
  float anim_blend = 0.0f;
  uint8_t character = 1;
};

struct NetPlayerRemove {
  uint32_t player_id = 0;
};

struct NetChunkCoord {
  uint32_t body_id = 1;
  uint8_t face = 0;
  uint8_t shell = 0;
  int16_t x = 0;
  int16_t y = 0;
  int16_t z = 0;
};

inline bool operator==(const NetChunkCoord &lhs, const NetChunkCoord &rhs) {
  return lhs.body_id == rhs.body_id && lhs.face == rhs.face &&
         lhs.shell == rhs.shell && lhs.x == rhs.x && lhs.y == rhs.y &&
         lhs.z == rhs.z;
}

struct NetChunkInterest {
  uint32_t body_id = 1;
  uint8_t face = 0;
  uint8_t shell = 0;
  int16_t center_x = 0;
  int16_t center_y = 0;
  int16_t center_z = 0;
  uint8_t radius = 2;
};

enum class NetChunkContentType : uint8_t {
  ProceduralFlat = 0,
  SphericalPlanet = 1
};

struct NetChunkState {
  NetChunkCoord coord{};
  uint32_t version = 0;
  uint64_t world_seed = 0;
  uint8_t content_type =
      static_cast<uint8_t>(NetChunkContentType::ProceduralFlat);
  uint8_t reserved[7]{};
};

struct NetDebugStats {
  uint64_t tx_packets_total = 0;
  uint64_t tx_bytes_total = 0;
  uint64_t rx_packets_total = 0;
  uint64_t rx_bytes_total = 0;
  uint64_t invalid_packets_total = 0;
  uint32_t tx_packets_per_sec = 0;
  uint32_t tx_bytes_per_sec = 0;
  uint32_t rx_packets_per_sec = 0;
  uint32_t rx_bytes_per_sec = 0;
  uint32_t snapshots_sent_per_sec = 0;
  uint32_t player_state_broadcasts_per_sec = 0;
};

template <size_t N> void net_copy_cstr(char (&dst)[N], const char *src) {
  std::memset(dst, 0, N);
  if (!src || N == 0) {
    return;
  }
  std::strncpy(dst, src, N - 1);
}
