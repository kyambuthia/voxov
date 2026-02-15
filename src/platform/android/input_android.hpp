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

private:
    glm::vec2 left_stick = glm::vec2(0.0f);
    glm::vec2 right_stick = glm::vec2(0.0f);
    bool jump = false;
    bool prev_jump = false;
    bool sprint = false;
};
