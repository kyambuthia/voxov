#include "platform/android/input_android.hpp"

InputState AndroidInputBackend::poll() {
    InputState out{};
    out.move = left_stick;
    out.look_delta = right_stick;
    out.jump_held = jump;
    out.jump_pressed = jump && !prev_jump;
    out.sprint_held = sprint;
    out.look_mode = true;
    prev_jump = jump;
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
