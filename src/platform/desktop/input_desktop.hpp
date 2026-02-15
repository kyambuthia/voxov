#pragma once

#include "engine_input/input_state.hpp"

struct GLFWwindow;

class DesktopInputBackend : public IInputBackend {
public:
    explicit DesktopInputBackend(GLFWwindow *window_handle);
    InputState poll() override;

private:
    GLFWwindow *window = nullptr;
    bool look_mode = false;
    bool prev_rmb_down = false;
    bool prev_space_down = false;
    double prev_mouse_x = 0.0;
    double prev_mouse_y = 0.0;
    bool mouse_initialized = false;
};
