#include "platform.hpp"

Platform::Platform() {}

Platform::~Platform() {}

void Platform::init() {}

void Platform::shutdown() {}

PlatformFrame Platform::begin_frame() {
    PlatformFrame frame{};
    frame.frame_index = frame_index++;
    frame.dt_seconds = 0.016f;
    return frame;
}

PlatformInput Platform::poll_input() {
    PlatformInput input{};
    return input;
}
