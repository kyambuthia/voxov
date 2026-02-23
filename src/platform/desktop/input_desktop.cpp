#include "platform/desktop/input_desktop.hpp"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

DesktopInputBackend::DesktopInputBackend(GLFWwindow *window_handle)
    : window(window_handle) {}

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
}

InputState DesktopInputBackend::poll() {
    InputState out{};

    if (!window) {
        return out;
    }

    const bool rmb_down = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    const bool window_focused = glfwGetWindowAttrib(window, GLFW_FOCUSED) == GLFW_TRUE;
    const bool rmb_pressed = rmb_down && !prev_rmb_down;
    prev_rmb_down = rmb_down;

    if (rmb_pressed) {
        look_capture_enabled = true;
    }

    const bool active_look_mode = window_focused && look_capture_enabled;
    set_pointer_lock(active_look_mode);
    out.look_mode = active_look_mode;
    out.rmb_down = rmb_down;
    out.pointer_locked = pointer_locked;
    out.look_enabled = active_look_mode;

    out.key_w = glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS;
    out.key_a = glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS;
    out.key_s = glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS;
    out.key_d = glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS;

    if (out.key_w) {
        out.move.y += 1.0f;
    }
    if (out.key_s) {
        out.move.y -= 1.0f;
    }
    if (out.key_d) {
        out.move.x += 1.0f;
    }
    if (out.key_a) {
        out.move.x -= 1.0f;
    }

    if (out.move.x != 0.0f || out.move.y != 0.0f) {
        out.move = glm::normalize(out.move);
    }

    const bool space_down = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
    out.jump_held = space_down;
    out.jump_pressed = space_down && !prev_space_down;
    prev_space_down = space_down;

    const bool escape_down = glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS;
    out.menu_toggle_pressed = escape_down && !prev_escape_down;
    if (escape_down && !prev_escape_down) {
        look_capture_enabled = false;
        set_pointer_lock(false);
    }
    prev_escape_down = escape_down;

    const bool up_down = glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS;
    out.menu_up_pressed = up_down && !prev_up_down;
    prev_up_down = up_down;

    const bool down_down = glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS;
    out.menu_down_pressed = down_down && !prev_down_down;
    prev_down_down = down_down;

    const bool enter_down = glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_PRESS;
    out.menu_select_pressed = enter_down && !prev_enter_down;
    prev_enter_down = enter_down;

    const bool f_down = glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS;
    out.interact_pressed = f_down && !prev_f_down;
    prev_f_down = f_down;

    const bool f1_down = glfwGetKey(window, GLFW_KEY_F1) == GLFW_PRESS;
    out.debug_toggle_pressed = f1_down && !prev_f1_down;
    prev_f1_down = f1_down;

    const bool f2_down = glfwGetKey(window, GLFW_KEY_F2) == GLFW_PRESS;
    out.debug_xray_toggle_pressed = f2_down && !prev_f2_down;
    prev_f2_down = f2_down;

    const bool f3_down = glfwGetKey(window, GLFW_KEY_F3) == GLFW_PRESS;
    out.debug_collision_only_toggle_pressed = f3_down && !prev_f3_down;
    prev_f3_down = f3_down;

    const bool f4_down = glfwGetKey(window, GLFW_KEY_F4) == GLFW_PRESS;
    out.debug_freeze_toggle_pressed = f4_down && !prev_f4_down;
    prev_f4_down = f4_down;

    const bool f5_down = glfwGetKey(window, GLFW_KEY_F5) == GLFW_PRESS;
    out.debug_reconcile_toggle_pressed = f5_down && !prev_f5_down;
    prev_f5_down = f5_down;

    out.sprint_held = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                      glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
    out.crouch_held = glfwGetKey(window, GLFW_KEY_C) == GLFW_PRESS ||
                      glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS;

    double x = 0.0;
    double y = 0.0;
    glfwGetCursorPos(window, &x, &y);
    if (!mouse_initialized) {
        prev_mouse_x = x;
        prev_mouse_y = y;
        mouse_initialized = true;
    }

    const float mouse_dx = static_cast<float>(x - prev_mouse_x);
    const float mouse_dy = static_cast<float>(y - prev_mouse_y);
    prev_mouse_x = x;
    prev_mouse_y = y;

    if (pointer_locked) {
        out.look_delta.x = mouse_dx;
        out.look_delta.y = mouse_dy;
    }

    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) {
        out.zoom_delta += 0.08f;
    }
    if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) {
        out.zoom_delta -= 0.08f;
    }

    return out;
}
