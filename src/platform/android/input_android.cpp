#include "platform/android/input_android.hpp"

InputState AndroidInputBackend::poll() {
    InputState out{};
    out.move = left_stick;
    out.look_delta = right_stick;
    out.jump_held = jump;
    out.jump_pressed = jump && !prev_jump;
    out.sprint_held = sprint;
    out.crouch_held = crouch;
    out.look_mode = true;
    out.menu_toggle_pressed = menu_toggle && !prev_menu_toggle;
    out.menu_up_pressed = menu_up && !prev_menu_up;
    out.menu_down_pressed = menu_down && !prev_menu_down;
    out.menu_select_pressed = menu_select && !prev_menu_select;
    out.debug_toggle_pressed = debug_toggle && !prev_debug_toggle;
    out.debug_xray_toggle_pressed = debug_xray_toggle && !prev_debug_xray_toggle;
    out.debug_collision_only_toggle_pressed = debug_collision_only_toggle && !prev_debug_collision_only_toggle;
    out.debug_freeze_toggle_pressed = debug_freeze_toggle && !prev_debug_freeze_toggle;
    prev_jump = jump;
    prev_menu_toggle = menu_toggle;
    prev_menu_up = menu_up;
    prev_menu_down = menu_down;
    prev_menu_select = menu_select;
    prev_debug_toggle = debug_toggle;
    prev_debug_xray_toggle = debug_xray_toggle;
    prev_debug_collision_only_toggle = debug_collision_only_toggle;
    prev_debug_freeze_toggle = debug_freeze_toggle;
    return out;
}

void AndroidInputBackend::set_left_stick(glm::vec2 value) {
    left_stick = value;
}

void AndroidInputBackend::set_right_stick(glm::vec2 value) {
    right_stick = value;
}

void AndroidInputBackend::set_jump(bool pressed) {
    jump = pressed;
}

void AndroidInputBackend::set_sprint(bool enabled) {
    sprint = enabled;
}

void AndroidInputBackend::set_crouch(bool enabled) {
    crouch = enabled;
}

void AndroidInputBackend::set_menu_toggle(bool pressed) {
    menu_toggle = pressed;
}

void AndroidInputBackend::set_menu_up(bool pressed) {
    menu_up = pressed;
}

void AndroidInputBackend::set_menu_down(bool pressed) {
    menu_down = pressed;
}

void AndroidInputBackend::set_menu_select(bool pressed) {
    menu_select = pressed;
}

void AndroidInputBackend::set_debug_toggle(bool pressed) {
    debug_toggle = pressed;
}

void AndroidInputBackend::set_debug_xray_toggle(bool pressed) {
    debug_xray_toggle = pressed;
}

void AndroidInputBackend::set_debug_collision_only_toggle(bool pressed) {
    debug_collision_only_toggle = pressed;
}

void AndroidInputBackend::set_debug_freeze_toggle(bool pressed) {
    debug_freeze_toggle = pressed;
}
