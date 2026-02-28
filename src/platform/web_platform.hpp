#pragma once

#include <cstdint>

class WebPlatform {
public:
    bool init(int32_t width = 1280, int32_t height = 720);
    void shutdown();
    bool poll_events();
    void on_resize(int32_t width, int32_t height);
    void set_focused(bool focused);

    bool active() const;
    int32_t width() const;
    int32_t height() const;
    uint64_t tick() const;

private:
    int32_t canvas_width = 1280;
    int32_t canvas_height = 720;
    uint64_t tick_counter = 0;
    bool initialized = false;
    bool focused = true;
};
