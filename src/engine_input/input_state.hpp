#pragma once

#include <glm/glm.hpp>

struct InputState {
    glm::vec2 move = glm::vec2(0.0f);
    glm::vec2 look_delta = glm::vec2(0.0f);
    bool key_w = false;
    bool key_a = false;
    bool key_s = false;
    bool key_d = false;
    bool jump_pressed = false;
    bool jump_held = false;
    bool interact_pressed = false;
    bool sprint_held = false;
    bool crouch_held = false;
    bool look_mode = false;
    bool rmb_down = false;
    bool pointer_locked = false;
    bool look_enabled = false;
    bool menu_toggle_pressed = false;
    bool menu_up_pressed = false;
    bool menu_down_pressed = false;
    bool menu_select_pressed = false;
    bool debug_toggle_pressed = false;
    bool debug_xray_toggle_pressed = false;
    bool debug_collision_only_toggle_pressed = false;
    bool debug_freeze_toggle_pressed = false;
    bool debug_reconcile_toggle_pressed = false;
    float zoom_delta = 0.0f;
};

class IInputBackend {
public:
    virtual ~IInputBackend() = default;
    virtual InputState poll() = 0;
};
