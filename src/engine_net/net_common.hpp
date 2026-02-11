#pragma once

#include <cstdint>

struct NetTickInput {
    uint32_t tick = 0;
    float move_x = 0.0f;
    float move_y = 0.0f;
};

struct NetSnapshot {
    uint32_t tick = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

enum class NetMsgType : uint8_t {
    Input = 1,
    Snapshot = 2
};
