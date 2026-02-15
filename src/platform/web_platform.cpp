#include "platform/web_platform.hpp"

bool WebPlatform::init() {
#if defined(__EMSCRIPTEN__)
    return true;
#else
    return false;
#endif
}

void WebPlatform::shutdown() {}

bool WebPlatform::poll_events() {
#if defined(__EMSCRIPTEN__)
    return true;
#else
    return false;
#endif
}
