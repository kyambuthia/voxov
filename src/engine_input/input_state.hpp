#pragma once

#include <glm/glm.hpp>

struct InputState {
    glm::vec2 move = glm::vec2(0.0f);
    glm::vec2 look_delta = glm::vec2(0.0f);
    bool jump_pressed = false;
    bool jump_held = false;
    bool sprint_held = false;
    bool look_mode = false;
    bool rmb_down = false;
    bool pointer_locked = false;
    bool look_enabled = false;
    float zoom_delta = 0.0f;
};

class IInputBackend {
public:
    virtual ~IInputBackend() = default;
    virtual InputState poll() = 0;
};
