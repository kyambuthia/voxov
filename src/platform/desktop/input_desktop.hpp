#pragma once

#include "engine_input/input_state.hpp"

struct GLFWwindow;

class DesktopInputBackend : public IInputBackend {
public:
    explicit DesktopInputBackend(GLFWwindow *window_handle);
    InputState poll() override;

private:
    void set_pointer_lock(bool enabled);

    GLFWwindow *window = nullptr;
    bool pointer_locked = false;
    bool prev_space_down = false;
    bool prev_escape_down = false;
    bool prev_up_down = false;
    bool prev_down_down = false;
    bool prev_enter_down = false;
    bool prev_f1_down = false;
    bool prev_f2_down = false;
    bool prev_f3_down = false;
    bool prev_f4_down = false;
    bool prev_rmb_down = false;
    bool look_capture_enabled = true;
    double prev_mouse_x = 0.0;
    double prev_mouse_y = 0.0;
    bool mouse_initialized = false;
};
