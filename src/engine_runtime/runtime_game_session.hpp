#pragma once

#include "engine_core/timing.hpp"

#include <cstdint>
#include <functional>

struct RuntimeGameSessionStepContext {
    uint64_t tick = 0;
    float dt = 0.0f;
};

struct RuntimeGameSessionCallbacks {
    std::function<void()> pump_server;
    std::function<void(const RuntimeGameSessionStepContext &step)> simulate_step;
};

class RuntimeGameSession {
public:
    void reset(double fixed_dt = 1.0 / 60.0);
    void advance(double frame_dt, const RuntimeGameSessionCallbacks &callbacks);

    const FixedStep &fixed_step() const;
    double fixed_cpu_ms() const;
    uint32_t fixed_steps_last_frame() const;

private:
    FixedStep fixed{};
    double last_fixed_cpu_ms = 0.0;
    uint32_t last_fixed_steps = 0;
};
