#include "platform/desktop/input_desktop.hpp"
#include "platform/platform.hpp"
#include "platform/platform_input_state.hpp"

#include <glm/glm.hpp>

DesktopInputBackend::DesktopInputBackend(DesktopPlatform &platform)
    : platform_(platform) {}

void DesktopInputBackend::set_pointer_lock(bool enabled) {
    if (pointer_locked == enabled) {
        return;
    }
    pointer_locked = enabled;
    platform_.set_mouse_lock(enabled);
    mouse_initialized = false;
}

InputState DesktopInputBackend::poll() {
    const PlatformInputSnapshot &snap = platform_.input();
    InputState out{};

    const bool rmb_down = snap.mouse_down.test(1); // SAPP_MOUSEBUTTON_RIGHT
    const bool lmb_down = snap.mouse_down.test(0); // SAPP_MOUSEBUTTON_LEFT
    const bool any_mouse_down = snap.mouse_down.any();
    const bool rmb_pressed = rmb_down && !prev_rmb_down;
    const bool lmb_pressed = lmb_down && !prev_lmb_down;
    prev_rmb_down = rmb_down;
    prev_lmb_down = lmb_down;

    if (rmb_pressed || any_mouse_down) {
        look_capture_enabled = true;
    }

    const bool active_look_mode = snap.focused && look_capture_enabled;
    set_pointer_lock(active_look_mode);
    out.look_mode = active_look_mode;
    out.rmb_down = rmb_down;
    out.left_click_pressed = lmb_pressed;
    out.right_click_pressed = rmb_pressed;
    out.pointer_locked = pointer_locked;
    out.look_enabled = active_look_mode;

    // WASD — using PlatformKey enum
    auto k = [&](PlatformKey key) -> bool {
        return snap.keys_down.test(static_cast<size_t>(key));
    };

    out.key_w = k(PlatformKey::W);
    out.key_a = k(PlatformKey::A);
    out.key_s = k(PlatformKey::S);
    out.key_d = k(PlatformKey::D);

    if (out.key_w) out.move.y += 1.0f;
    if (out.key_s) out.move.y -= 1.0f;
    if (out.key_d) out.move.x += 1.0f;
    if (out.key_a) out.move.x -= 1.0f;

    if (out.move.x != 0.0f || out.move.y != 0.0f) {
        out.move = glm::normalize(out.move);
    }

    const bool space_down = k(PlatformKey::Space);
    out.jump_held = space_down;
    out.jump_pressed = space_down && !prev_space_down;
    prev_space_down = space_down;

    const bool escape_down = k(PlatformKey::Escape);
    out.menu_toggle_pressed = escape_down && !prev_escape_down;
    if (escape_down && !prev_escape_down) {
        look_capture_enabled = false;
        set_pointer_lock(false);
    }
    prev_escape_down = escape_down;

    const bool up_down = k(PlatformKey::Up) || k(PlatformKey::W);
    out.menu_up_pressed = up_down && !prev_up_down;
    prev_up_down = up_down;

    const bool down_down = k(PlatformKey::Down) || k(PlatformKey::S);
    out.menu_down_pressed = down_down && !prev_down_down;
    prev_down_down = down_down;

    const bool enter_down = k(PlatformKey::Enter);
    out.menu_select_pressed = enter_down && !prev_enter_down;
    prev_enter_down = enter_down;

    const bool f_down = k(PlatformKey::F);
    const bool e_down = k(PlatformKey::E);
    out.interact_pressed = (f_down && !prev_f_down) || (e_down && !prev_e_down);
    prev_f_down = f_down;
    prev_e_down = e_down;

    // Debug toggle keys
    const bool f1_down = k(PlatformKey::F1);
    out.debug_toggle_pressed = f1_down && !prev_f1_down;
    prev_f1_down = f1_down;

    const bool f2_down = k(PlatformKey::F2);
    out.debug_xray_toggle_pressed = f2_down && !prev_f2_down;
    prev_f2_down = f2_down;

    const bool f3_down = k(PlatformKey::F3);
    out.debug_collision_only_toggle_pressed = f3_down && !prev_f3_down;
    prev_f3_down = f3_down;

    const bool f4_down = k(PlatformKey::F4);
    out.debug_freeze_toggle_pressed = f4_down && !prev_f4_down;
    prev_f4_down = f4_down;

    const bool f5_down = k(PlatformKey::F5);
    out.debug_reconcile_toggle_pressed = f5_down && !prev_f5_down;
    prev_f5_down = f5_down;

    const bool f12_down = k(PlatformKey::F12);
    out.screenshot_requested = f12_down && !prev_f12_down;
    prev_f12_down = f12_down;

    out.sprint_held = k(PlatformKey::LeftShift);
    out.crouch_held = k(PlatformKey::C) || k(PlatformKey::LeftControl);

    // Mouse delta
    const float mouse_dx = snap.mouse_delta.x;
    const float mouse_dy = snap.mouse_delta.y;
    if (!mouse_initialized) {
        mouse_initialized = true;
    }

    if (pointer_locked) {
        out.look_delta.x = mouse_dx;
        out.look_delta.y = mouse_dy;
    }

    if (k(PlatformKey::Q)) {
        out.zoom_delta += 0.08f;
    }
    if (k(PlatformKey::R)) {
        out.zoom_delta -= 0.08f;
    }

    return out;
}
