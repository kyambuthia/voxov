#include "platform/android_platform.hpp"

bool AndroidPlatform::init(int32_t width, int32_t height) {
    surface_width = (width > 0) ? width : 1;
    surface_height = (height > 0) ? height : 1;
    tick_count = 0;
    paused = false;
    initialized = true;
    return true;
}

void AndroidPlatform::shutdown() {
    initialized = false;
    paused = false;
    tick_count = 0;
}

bool AndroidPlatform::poll_events() {
    if (!initialized || paused) {
        return false;
    }
    ++tick_count;
    return true;
}

void AndroidPlatform::on_pause() {
    paused = true;
}

void AndroidPlatform::on_resume() {
    if (!initialized) {
        return;
    }
    paused = false;
}

void AndroidPlatform::on_resize(int32_t width, int32_t height) {
    surface_width = (width > 0) ? width : 1;
    surface_height = (height > 0) ? height : 1;
}

bool AndroidPlatform::active() const {
    return initialized && !paused;
}

int32_t AndroidPlatform::width() const {
    return surface_width;
}

int32_t AndroidPlatform::height() const {
    return surface_height;
}

double AndroidPlatform::frame_time_seconds() const {
    return 1.0 / 60.0;
}
