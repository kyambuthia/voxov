#include "platform/ios_platform.hpp"

bool IosPlatform::init() {
#if defined(__APPLE__) && defined(TARGET_OS_IPHONE)
    return true;
#else
    return false;
#endif
}

void IosPlatform::shutdown() {}

bool IosPlatform::poll_events() {
#if defined(__APPLE__) && defined(TARGET_OS_IPHONE)
    return true;
#else
    return false;
#endif
}
