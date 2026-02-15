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
};

struct NetSnapshot {
    uint32_t player_id = 0;
    uint32_t tick = 0;
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
    PlayerState = 6
};

struct NetAssignPlayer {
    uint32_t player_id = 0;
};

struct NetPlayerState {
    uint32_t player_id = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    float vz = 0.0f;
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
