#pragma once

#include "engine_input/input_state.hpp"

#include <glm/glm.hpp>

class AndroidInputBackend : public IInputBackend {
public:
    InputState poll() override;

    void set_left_stick(glm::vec2 value);
    void set_right_stick(glm::vec2 value);
    void set_jump(bool pressed);
    void set_sprint(bool enabled);
    void set_crouch(bool enabled);
    void set_menu_toggle(bool pressed);
    void set_menu_up(bool pressed);
    void set_menu_down(bool pressed);
    void set_menu_select(bool pressed);

private:
    glm::vec2 left_stick = glm::vec2(0.0f);
    glm::vec2 right_stick = glm::vec2(0.0f);
    bool jump = false;
    bool prev_jump = false;
    bool sprint = false;
    bool crouch = false;
    bool menu_toggle = false;
    bool prev_menu_toggle = false;
    bool menu_up = false;
    bool prev_menu_up = false;
    bool menu_down = false;
    bool prev_menu_down = false;
    bool menu_select = false;
    bool prev_menu_select = false;
};
