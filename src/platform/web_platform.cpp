#include "platform/web_platform.hpp"

bool WebPlatform::init(int32_t width, int32_t height) {
    canvas_width = (width > 0) ? width : 1;
    canvas_height = (height > 0) ? height : 1;
    tick_counter = 0;
    focused = true;
    initialized = true;
    return true;
}

void WebPlatform::shutdown() {
    initialized = false;
    focused = false;
    tick_counter = 0;
}

bool WebPlatform::poll_events() {
    if (!initialized || !focused) {
        return false;
    }
    ++tick_counter;
    return true;
}

void WebPlatform::on_resize(int32_t width, int32_t height) {
    canvas_width = (width > 0) ? width : 1;
    canvas_height = (height > 0) ? height : 1;
}

void WebPlatform::set_focused(bool is_focused) {
    focused = is_focused;
}

bool WebPlatform::active() const {
    return initialized && focused;
}

int32_t WebPlatform::width() const {
    return canvas_width;
}

int32_t WebPlatform::height() const {
    return canvas_height;
}

uint64_t WebPlatform::tick() const {
    return tick_counter;
}
