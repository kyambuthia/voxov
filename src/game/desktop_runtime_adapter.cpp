#include "game/desktop_runtime_adapter.hpp"
#include "platform/platform.hpp"

#include <glm/glm.hpp>

namespace {
InputState poll_secondary_split_input(DesktopPlatform &platform) {
    InputState out{};

    if (platform.is_key_down(73)) { // I
        out.move.y += 1.0f;
    }
    if (platform.is_key_down(75)) { // K
        out.move.y -= 1.0f;
    }
    if (platform.is_key_down(76)) { // L
        out.move.x += 1.0f;
    }
    if (platform.is_key_down(74)) { // J
        out.move.x -= 1.0f;
    }
    if (out.move.x != 0.0f || out.move.y != 0.0f) {
        out.move = glm::normalize(out.move);
    }

    constexpr float look_speed = 5.0f;
    if (platform.is_key_down(263)) { // LEFT
        out.look_delta.x -= look_speed;
    }
    if (platform.is_key_down(262)) { // RIGHT
        out.look_delta.x += look_speed;
    }
    if (platform.is_key_down(265)) { // UP
        out.look_delta.y -= look_speed;
    }
    if (platform.is_key_down(264)) { // DOWN
        out.look_delta.y += look_speed;
    }

    const bool rctrl_down =
        platform.is_key_down(341) || platform.is_key_down(341);
    out.jump_held = rctrl_down;
    out.jump_pressed = rctrl_down;
    out.sprint_held =
        platform.is_key_down(340) || // RSHIFT
        platform.is_key_down(47);    // SLASH
    out.crouch_held = platform.is_key_down(341); // RCTRL

    return out;
}
} // namespace

DesktopRuntimeInputAdapter::DesktopRuntimeInputAdapter(
    DesktopPlatform &platform,
    bool splitscreen_enabled)
    : platform_(platform),
      input_backend_(platform),
      splitscreen_enabled_(splitscreen_enabled) {}

GameRuntimeInputFrame DesktopRuntimeInputAdapter::poll_input() {
    GameRuntimeInputFrame frame{};
    frame.primary = input_backend_.poll();
    if (splitscreen_enabled_) {
        frame.secondary = poll_secondary_split_input(platform_);
    }
    return frame;
}

void DesktopRuntimeInputAdapter::set_splitscreen_enabled(bool enabled) {
    splitscreen_enabled_ = enabled;
}

DesktopRuntimePlatformAdapter::DesktopRuntimePlatformAdapter(DesktopPlatform &platform)
    : platform_(platform) {}

void DesktopRuntimePlatformAdapter::poll_events() {
    // sokol_app events are dispatched via the callback in main.cpp.
    // No explicit poll_events() needed.
}

bool DesktopRuntimePlatformAdapter::should_close() const {
    return platform_.should_close();
}

void *DesktopRuntimePlatformAdapter::native_window() {
    return platform_.native_window();
}
