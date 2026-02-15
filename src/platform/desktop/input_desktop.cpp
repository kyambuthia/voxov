#include "platform/desktop/input_desktop.hpp"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

std::unordered_map<GLFWwindow *, DesktopInputBackend *> DesktopInputBackend::instances;

DesktopInputBackend::DesktopInputBackend(GLFWwindow *window_handle)
    : window(window_handle) {
    if (window) {
        instances[window] = this;
        glfwSetCursorPosCallback(window, &DesktopInputBackend::cursor_position_callback);
    }
}

DesktopInputBackend::~DesktopInputBackend() {
    if (window) {
        glfwSetCursorPosCallback(window, nullptr);
        instances.erase(window);
    }
}

void DesktopInputBackend::cursor_position_callback(GLFWwindow *window, double x, double y) {
    auto it = instances.find(window);
    if (it != instances.end() && it->second) {
        it->second->on_cursor_position(x, y);
    }
}

void DesktopInputBackend::on_cursor_position(double x, double y) {
    if (!mouse_initialized) {
        prev_mouse_x = x;
        prev_mouse_y = y;
        mouse_initialized = true;
        return;
    }

    accum_look_x += static_cast<float>(x - prev_mouse_x);
    accum_look_y += static_cast<float>(y - prev_mouse_y);
    prev_mouse_x = x;
    prev_mouse_y = y;
}

void DesktopInputBackend::set_pointer_lock(bool enabled) {
    if (!window || pointer_locked == enabled) {
        return;
    }

    pointer_locked = enabled;
    glfwSetInputMode(window, GLFW_CURSOR, enabled ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    if (glfwRawMouseMotionSupported()) {
        glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, enabled ? GLFW_TRUE : GLFW_FALSE);
    }
    mouse_initialized = false;
    accum_look_x = 0.0f;
    accum_look_y = 0.0f;
}

InputState DesktopInputBackend::poll() {
    InputState out{};

    if (!window) {
        return out;
    }

    const bool rmb_down = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    if (rmb_down && !prev_rmb_down) {
        look_mode = !look_mode;
    }
    prev_rmb_down = rmb_down;

    const bool active_look_mode = look_mode || rmb_down;
    set_pointer_lock(active_look_mode);
    out.look_mode = active_look_mode;
    out.rmb_down = rmb_down;
    out.pointer_locked = pointer_locked;
    out.look_enabled = active_look_mode;

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
        out.move.y += 1.0f;
    }
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
        out.move.y -= 1.0f;
    }
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
        out.move.x += 1.0f;
    }
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
        out.move.x -= 1.0f;
    }

    if (out.move.x != 0.0f || out.move.y != 0.0f) {
        out.move = glm::normalize(out.move);
    }

    const bool space_down = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
    out.jump_held = space_down;
    out.jump_pressed = space_down && !prev_space_down;
    prev_space_down = space_down;

    out.sprint_held = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                      glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;

    if (active_look_mode) {
        out.look_delta.x = accum_look_x;
        out.look_delta.y = accum_look_y;
    }
    accum_look_x = 0.0f;
    accum_look_y = 0.0f;

    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) {
        out.zoom_delta += 0.08f;
    }
    if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) {
        out.zoom_delta -= 0.08f;
    }

    return out;
}
