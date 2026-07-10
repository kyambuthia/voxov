#include "engine_runtime/runtime_game_session.hpp"

#include <algorithm>
#include <chrono>

namespace {
using PerfClock = std::chrono::steady_clock;

double elapsed_ms(const PerfClock::time_point &start,
                  const PerfClock::time_point &end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}
} // namespace

void RuntimeGameSession::reset(double fixed_dt) {
    fixed = FixedStep{};
    if (fixed_dt > 0.0) {
        fixed.fixed_dt = fixed_dt;
    }
    last_fixed_cpu_ms = 0.0;
    last_fixed_steps = 0;
}

void RuntimeGameSession::advance(
    double frame_dt,
    const RuntimeGameSessionCallbacks &callbacks) {
    last_fixed_steps = 0;
    const PerfClock::time_point fixed_cpu_start = PerfClock::now();

    // Clamp frame_dt to 250 ms to prevent unbounded catch-up work after
    // a giant frame spike. Matches FixedStepCounter::consume() clamp pattern.
    fixed.accumulator += std::clamp(frame_dt, 0.0, 0.25);
    while (fixed.accumulator >= fixed.fixed_dt) {
        if (callbacks.pump_server) {
            callbacks.pump_server();
        }
        if (callbacks.simulate_step) {
            callbacks.simulate_step(RuntimeGameSessionStepContext{
                .tick = fixed.tick,
                .dt = static_cast<float>(fixed.fixed_dt),
            });
        }
        fixed.accumulator -= fixed.fixed_dt;
        fixed.tick += 1;
        last_fixed_steps += 1;
    }

    last_fixed_cpu_ms =
        last_fixed_steps > 0 ? elapsed_ms(fixed_cpu_start, PerfClock::now())
                             : 0.0;
}

const FixedStep &RuntimeGameSession::fixed_step() const { return fixed; }

double RuntimeGameSession::fixed_cpu_ms() const { return last_fixed_cpu_ms; }

uint32_t RuntimeGameSession::fixed_steps_last_frame() const {
    return last_fixed_steps;
}
