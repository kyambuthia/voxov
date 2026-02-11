#pragma once

#include <cstdint>

struct PlatformInput {
    float move_x = 0.0f;
    float move_y = 0.0f;
    bool jump = false;
};

struct PlatformFrame {
    float dt_seconds = 0.0f;
    uint64_t frame_index = 0;
};

class Platform {
public:
    Platform();
    ~Platform();

    void init();
    void shutdown();
    PlatformFrame begin_frame();
    PlatformInput poll_input();

private:
    uint64_t frame_index = 0;
};
