#pragma once

#include <cstdint>

struct SimPlayerState {
    uint32_t id = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    float vz = 0.0f;
};

class SimWorld {
public:
    SimWorld();
    ~SimWorld();

    void reset();
    void fixed_update(float dt_seconds);
};
