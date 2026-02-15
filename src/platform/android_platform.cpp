#include "platform/android_platform.hpp"

bool AndroidPlatform::init() {
#if defined(__ANDROID__)
    return true;
#else
    return false;
#endif
}

void AndroidPlatform::shutdown() {}

bool AndroidPlatform::poll_events() {
#if defined(__ANDROID__)
    return true;
#else
    return false;
#endif
}
