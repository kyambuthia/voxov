#include "platform/console_platform.hpp"

bool ConsolePlatform::init() {
    return false;
}

void ConsolePlatform::shutdown() {}

bool ConsolePlatform::poll_events() {
    return false;
}
