#pragma once

#include <cstdint>
#include <cstring>

enum class NetChannel : uint8_t {
    Reliable = 0,
    Unreliable = 1
};

struct NetTickInput {
    uint32_t tick = 0;
    float move_x = 0.0f;
    float move_y = 0.0f;
    uint8_t action_flags = 0;
};

enum class NetInputFlags : uint8_t {
    JumpHeld = 1u << 0u,
    JumpPressed = 1u << 1u,
    SprintHeld = 1u << 2u,
    CrouchHeld = 1u << 3u
};

inline uint8_t net_flag(NetInputFlags flag) {
    return static_cast<uint8_t>(flag);
}

inline bool net_flag_set(uint8_t flags, NetInputFlags flag) {
    return (flags & net_flag(flag)) != 0;
}

struct NetSnapshot {
    uint32_t player_id = 0;
    uint32_t tick = 0;
    uint32_t sequence = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
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
    ProtocolInfo = 8
};

constexpr uint32_t k_net_packet_magic = 0x564F5832u; // "VOX2"
constexpr uint16_t k_net_protocol_version = 1u;

enum class NetFeatureFlags : uint16_t {
    None = 0,
    InterestFilteredReplication = 1u << 0u,
    ChunkStreaming = 1u << 1u
};

inline uint16_t net_feature(NetFeatureFlags feature) {
    return static_cast<uint16_t>(feature);
}

inline bool net_feature_set(uint16_t flags, NetFeatureFlags feature) {
    return (flags & net_feature(feature)) != 0;
}

#pragma pack(push, 1)
struct NetPacketHeader {
    uint32_t magic = k_net_packet_magic;
    uint16_t version = k_net_protocol_version;
    uint8_t type = 0;
    uint8_t payload_size = 0;
};
#pragma pack(pop)

inline NetPacketHeader net_make_header(NetMsgType type, uint8_t payload_size) {
    NetPacketHeader header{};
    header.type = static_cast<uint8_t>(type);
    header.payload_size = payload_size;
    return header;
}

inline bool net_header_basic_valid(const NetPacketHeader &header) {
    return header.magic == k_net_packet_magic &&
           header.version == k_net_protocol_version;
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

struct NetPlayerState {
    uint32_t player_id = 0;
    uint32_t tick = 0;
    uint32_t sequence = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    float vz = 0.0f;
    uint8_t anim_state = 0;
    float anim_phase = 0.0f;
    float anim_blend = 0.0f;
};

struct NetPlayerRemove {
    uint32_t player_id = 0;
};

struct NetChunkCoord {
    int16_t x = 0;
    int16_t z = 0;
};

struct NetChunkInterest {
    int16_t center_x = 0;
    int16_t center_z = 0;
    uint8_t radius = 2;
};

struct NetChunkState {
    NetChunkCoord coord{};
    uint32_t version = 0;
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

template <typename T>
bool net_write_pod(uint8_t *dst, size_t dst_size, const T &value) {
    if (dst_size < sizeof(T)) {
        return false;
    }
    std::memcpy(dst, &value, sizeof(T));
    return true;
}

template <typename T>
bool net_read_pod(const uint8_t *src, size_t src_size, T &out_value) {
    if (src_size < sizeof(T)) {
        return false;
    }
    std::memcpy(&out_value, src, sizeof(T));
    return true;
}
