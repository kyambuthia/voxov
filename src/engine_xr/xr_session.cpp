#include "engine_xr/xr_session.hpp"

#include <cmath>

bool XrSession::init() {
    initialized = true;
    current_frame_index = 0;
    stereo_views = {};
    return true;
}

void XrSession::shutdown() {
    initialized = false;
    current_frame_index = 0;
    stereo_views = {};
}

void XrSession::begin_frame(double predicted_display_time_seconds) {
    if (!initialized) {
        return;
    }

    // Keep deterministic placeholder eye offsets until OpenXR/visionOS backends
    // are introduced; this preserves a stable API for stereo consumers.
    const float eye_offset = 0.032f;
    const float t = static_cast<float>(predicted_display_time_seconds);
    const float subtle_head_bob = 0.005f * std::sin(t * 1.5f);

    stereo_views[0].position = glm::vec3(-eye_offset, subtle_head_bob, 0.0f);
    stereo_views[1].position = glm::vec3(eye_offset, subtle_head_bob, 0.0f);
    stereo_views[0].orientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    stereo_views[1].orientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

    ++current_frame_index;
}

bool XrSession::active() const {
    return initialized;
}

uint64_t XrSession::frame_index() const {
    return current_frame_index;
}

const std::array<XrViewPose, 2> &XrSession::views() const {
    return stereo_views;
}
