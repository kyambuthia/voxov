#pragma once

#include <cstdint>

class AndroidPlatform {
public:
    bool init(int32_t width = 1280, int32_t height = 720);
    void shutdown();
    bool poll_events();
    void on_pause();
    void on_resume();
    void on_resize(int32_t width, int32_t height);

    bool active() const;
    int32_t width() const;
    int32_t height() const;
    double frame_time_seconds() const;

private:
    int32_t surface_width = 1280;
    int32_t surface_height = 720;
    uint64_t tick_count = 0;
    bool initialized = false;
    bool paused = false;
};
