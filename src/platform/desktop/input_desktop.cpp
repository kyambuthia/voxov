#include "platform/desktop/input_desktop.hpp"
#include "platform/platform.hpp"

#include <glm/glm.hpp>

DesktopInputBackend::DesktopInputBackend(DesktopPlatform &platform)
    : platform_(platform) {}

void DesktopInputBackend::set_pointer_lock(bool enabled) {
    // sokol_app cursor mode: TBD in Phase 1 — for now, mouse delta
    // is handled differently in sokol (no GLFW_CURSOR_DISABLED equivalent
    // without platform-specific code). We track the locked state locally
    // and only report deltas when locked.
    if (pointer_locked == enabled) {
        return;
    }
    pointer_locked = enabled;
    mouse_initialized = false;
}

InputState DesktopInputBackend::poll() {
    InputState out{};

    // GLFW_KEY_RIGHT / MOUSE_BUTTON_RIGHT
    constexpr int kMouseRight = 1; // GLFW_MOUSE_BUTTON_RIGHT
    const bool rmb_down = platform_.is_mouse_button_down(kMouseRight);
    const bool window_focused = platform_.window_focused();
    const bool rmb_pressed = rmb_down && !prev_rmb_down;
    prev_rmb_down = rmb_down;

    if (rmb_pressed) {
        look_capture_enabled = true;
    }
    if (!rmb_down) {
        look_capture_enabled = false;
    }

    const bool active_look_mode = window_focused && rmb_down && look_capture_enabled;
    set_pointer_lock(active_look_mode);
    out.look_mode = active_look_mode;
    out.rmb_down = rmb_down;
    out.pointer_locked = pointer_locked;
    out.look_enabled = active_look_mode;

    // WASD — using GLFW-compatible key codes
    out.key_w = platform_.is_key_down(87);   // GLFW_KEY_W
    out.key_a = platform_.is_key_down(65);   // GLFW_KEY_A
    out.key_s = platform_.is_key_down(83);   // GLFW_KEY_S
    out.key_d = platform_.is_key_down(68);   // GLFW_KEY_D

    if (out.key_w) out.move.y += 1.0f;
    if (out.key_s) out.move.y -= 1.0f;
    if (out.key_d) out.move.x += 1.0f;
    if (out.key_a) out.move.x -= 1.0f;

    if (out.move.x != 0.0f || out.move.y != 0.0f) {
        out.move = glm::normalize(out.move);
    }

    const bool space_down = platform_.is_key_down(32);  // GLFW_KEY_SPACE
    out.jump_held = space_down;
    out.jump_pressed = space_down && !prev_space_down;
    prev_space_down = space_down;

    const bool escape_down = platform_.is_key_down(256); // GLFW_KEY_ESCAPE
    out.menu_toggle_pressed = escape_down && !prev_escape_down;
    if (escape_down && !prev_escape_down) {
        look_capture_enabled = false;
        set_pointer_lock(false);
    }
    prev_escape_down = escape_down;

    const bool up_down = platform_.is_key_down(265) || platform_.is_key_down(87);  // UP or W
    out.menu_up_pressed = up_down && !prev_up_down;
    prev_up_down = up_down;

    const bool down_down = platform_.is_key_down(264) || platform_.is_key_down(83); // DOWN or S
    out.menu_down_pressed = down_down && !prev_down_down;
    prev_down_down = down_down;

    const bool enter_down = platform_.is_key_down(257); // GLFW_KEY_ENTER
    out.menu_select_pressed = enter_down && !prev_enter_down;
    prev_enter_down = enter_down;

    const bool f_down = platform_.is_key_down(70);  // GLFW_KEY_F
    const bool e_down = platform_.is_key_down(69);  // GLFW_KEY_E
    out.interact_pressed = (f_down && !prev_f_down) || (e_down && !prev_e_down);
    prev_f_down = f_down;
    prev_e_down = e_down;

    // Debug toggle keys
    const bool f1_down = platform_.is_key_down(290);
    out.debug_toggle_pressed = f1_down && !prev_f1_down;
    prev_f1_down = f1_down;

    const bool f2_down = platform_.is_key_down(291);
    out.debug_xray_toggle_pressed = f2_down && !prev_f2_down;
    prev_f2_down = f2_down;

    const bool f3_down = platform_.is_key_down(292);
    out.debug_collision_only_toggle_pressed = f3_down && !prev_f3_down;
    prev_f3_down = f3_down;

    const bool f4_down = platform_.is_key_down(293);
    out.debug_freeze_toggle_pressed = f4_down && !prev_f4_down;
    prev_f4_down = f4_down;

    const bool f5_down = platform_.is_key_down(294);
    out.debug_reconcile_toggle_pressed = f5_down && !prev_f5_down;
    prev_f5_down = f5_down;

    out.sprint_held = platform_.is_key_down(340); // LEFT_SHIFT
    out.crouch_held = platform_.is_key_down(67) || platform_.is_key_down(341); // C or LCTRL

    // Mouse position/delta
    double x = 0.0, y = 0.0;
    platform_.mouse_position(x, y);
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

    if (platform_.is_key_down(81)) { // Q
        out.zoom_delta += 0.08f;
    }
    if (platform_.is_key_down(82)) { // R
        out.zoom_delta -= 0.08f;
    }

    return out;
}
