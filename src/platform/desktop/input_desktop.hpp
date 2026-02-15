#pragma once

#include "engine_input/input_state.hpp"

#include <unordered_map>

struct GLFWwindow;

class DesktopInputBackend : public IInputBackend {
public:
    explicit DesktopInputBackend(GLFWwindow *window_handle);
    ~DesktopInputBackend();
    InputState poll() override;

private:
    static void cursor_position_callback(GLFWwindow *window, double x, double y);
    void on_cursor_position(double x, double y);
    void set_pointer_lock(bool enabled);

    static std::unordered_map<GLFWwindow *, DesktopInputBackend *> instances;

    GLFWwindow *window = nullptr;
    bool look_mode = false;
    bool pointer_locked = false;
    bool prev_rmb_down = false;
    bool prev_space_down = false;
    double prev_mouse_x = 0.0;
    double prev_mouse_y = 0.0;
    bool mouse_initialized = false;
    float accum_look_x = 0.0f;
    float accum_look_y = 0.0f;
};
