#pragma once

#include <cstdint>

struct FixedStep {
    double fixed_dt = 1.0 / 60.0;
    double accumulator = 0.0;
    uint64_t tick = 0;
};

struct FrameTiming {
    double dt = 0.0;
    double alpha = 0.0;
};

class FramePacer {
public:
    void init(double target_fps);
    void begin_frame();
    void end_frame();
    double frame_dt() const;

private:
    double target_dt = 1.0 / 60.0;
    double last_frame_time = 0.0;
    double dt = 0.0;
};
