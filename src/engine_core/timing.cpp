#include "engine_core/timing.hpp"

#include <chrono>
#include <thread>

static double now_seconds() {
    using clock = std::chrono::high_resolution_clock;
    static const auto start = clock::now();
    const auto t = clock::now() - start;
    return std::chrono::duration<double>(t).count();
}

void FramePacer::init(double target_fps) {
    target_dt = 1.0 / target_fps;
    last_frame_time = now_seconds();
}

void FramePacer::begin_frame() {
    const double t = now_seconds();
    dt = t - last_frame_time;
    last_frame_time = t;
}

void FramePacer::end_frame() {
    const double t = now_seconds();
    const double frame_time = t - last_frame_time;
    const double remaining = target_dt - frame_time;
    if (remaining > 0.001) {
        std::this_thread::sleep_for(std::chrono::duration<double>(remaining - 0.001));
    }
    while ((now_seconds() - last_frame_time) < target_dt) {
        // busy wait last ~1ms for tighter pacing
    }
}

double FramePacer::frame_dt() const {
    return dt;
}
