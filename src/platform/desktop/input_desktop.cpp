#include "platform/desktop/input_desktop.hpp"
#include "platform/platform.hpp"
#include "platform/platform_input_state.hpp"

#include <cmath>
#include <cstdlib>

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

    // F6 is consumed here as well as by the engine so the platform can
    // change cursor policy in the same frame that the overlay opens.
    auto k = [&](PlatformKey key) -> bool {
        return snap.keys_down.test(static_cast<size_t>(key));
    };
    const bool f6_down = k(PlatformKey::F6);
    const bool sky_navigation_toggle_pressed = f6_down && !prev_f6_down;
    prev_f6_down = f6_down;
    if (sky_navigation_toggle_pressed) {
        sky_navigation_mode = !sky_navigation_mode;
        if (sky_navigation_mode) {
            // Sky navigation is a cursor UI, not a first-person look mode.
            look_capture_enabled = false;
        }
    }

    if (!sky_navigation_mode && (rmb_pressed || any_mouse_down)) {
        look_capture_enabled = true;
    }

    const bool active_look_mode = snap.focused && look_capture_enabled &&
                                  !sky_navigation_mode;
    set_pointer_lock(active_look_mode);
    out.look_mode = active_look_mode;
    out.rmb_down = rmb_down;
    out.left_click_pressed = lmb_pressed;
    out.right_click_pressed = rmb_pressed;
    out.pointer_locked = pointer_locked;
    out.look_enabled = active_look_mode;

    out.cursor_position = snap.mouse_pos;

    // WASD — using PlatformKey enum

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

    // Headless capture sessions: orbit walk + look so screenshot tooling can
    // inspect terrain without a human at the keyboard.
    if (std::getenv("VOXOV_CAPTURE_DEMO") != nullptr) {
        static uint32_t demo_frame = 0;
        ++demo_frame;
        const float t = static_cast<float>(demo_frame) * 0.016f;
        // Gentle forward walk with slow yaw so screenshots catch loaded terrain.
        out.move = glm::normalize(glm::vec2(
            std::sin(t * 0.25f) * 0.2f + 0.55f,
            0.35f));
        out.look_delta.x = std::sin(t * 0.12f) * 1.5f;
        // Small downward bias (positive look_delta.y lowers pitch).
        out.look_delta.y = 0.6f;
        out.look_mode = true;
        out.look_enabled = true;
        out.pointer_locked = true;
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
    out.sky_navigation_lock_pressed = out.menu_select_pressed;
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

    out.sky_navigation_toggle_pressed = sky_navigation_toggle_pressed;

    const bool left_down = k(PlatformKey::Left);
    const bool right_down = k(PlatformKey::Right);
    out.sky_navigation_prev_pressed =
        (left_down && !prev_left_down) || out.menu_up_pressed;
    out.sky_navigation_next_pressed =
        (right_down && !prev_right_down) || out.menu_down_pressed;
    prev_left_down = left_down;
    prev_right_down = right_down;

    const bool t_down = k(PlatformKey::T);
    out.engine_toggle_pressed = t_down && !prev_t_down;
    prev_t_down = t_down;

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

    // Automated orbital showcase used by visual regression captures. This is
    // intentionally separate from VOXOV_CAPTURE_DEMO, which remains a local
    // surface walk for terrain inspection.
    const bool capture_orbit =
        std::getenv("VOXOV_CAPTURE_ORBIT") != nullptr;
    const bool capture_flight =
        std::getenv("VOXOV_CAPTURE_FLIGHT") != nullptr;
    if (capture_orbit || capture_flight) {
        out.debug_freeze_toggle_pressed = false;
        // Hold the deterministic observation point. Moving forward while the
        // capture camera looks down drives the debug flyer through a 64 m
        // planet before the settled screenshot frame.
        out.move = glm::vec2(0.0f);
        out.sprint_held = false;
        out.jump_held = false;
        out.jump_pressed = false;
        out.crouch_held = false;
        out.look_delta = glm::vec2(0.0f);
        out.look_mode = true;
        out.look_enabled = true;
        out.pointer_locked = true;
    }

    return out;
}
