#pragma once

#include <array>
#include <cstdint>

#include <glm/vec3.hpp>
#include <glm/gtc/quaternion.hpp>

struct XrViewPose {
    glm::vec3 position = glm::vec3(0.0f);
    glm::quat orientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
};

class XrSession {
public:
    bool init();
    void shutdown();
    void begin_frame(double predicted_display_time_seconds);
    bool active() const;
    uint64_t frame_index() const;
    const std::array<XrViewPose, 2> &views() const;

private:
    std::array<XrViewPose, 2> stereo_views{};
    uint64_t current_frame_index = 0;
    bool initialized = false;
};
