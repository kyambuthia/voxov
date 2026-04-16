#include "game/desktop_runtime_adapter.hpp"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

namespace {
InputState poll_secondary_split_input(GLFWwindow *window) {
    InputState out{};
    if (window == nullptr) {
        return out;
    }

    if (glfwGetKey(window, GLFW_KEY_I) == GLFW_PRESS) {
        out.move.y += 1.0f;
    }
    if (glfwGetKey(window, GLFW_KEY_K) == GLFW_PRESS) {
        out.move.y -= 1.0f;
    }
    if (glfwGetKey(window, GLFW_KEY_L) == GLFW_PRESS) {
        out.move.x += 1.0f;
    }
    if (glfwGetKey(window, GLFW_KEY_J) == GLFW_PRESS) {
        out.move.x -= 1.0f;
    }
    if (out.move.x != 0.0f || out.move.y != 0.0f) {
        out.move = glm::normalize(out.move);
    }

    constexpr float look_speed = 5.0f;
    if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS) {
        out.look_delta.x -= look_speed;
    }
    if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) {
        out.look_delta.x += look_speed;
    }
    if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS) {
        out.look_delta.y -= look_speed;
    }
    if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS) {
        out.look_delta.y += look_speed;
    }

    const bool rctrl_down =
        glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS ||
        glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS;
    out.jump_held = rctrl_down;
    out.jump_pressed = rctrl_down;
    out.sprint_held =
        glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS ||
        glfwGetKey(window, GLFW_KEY_SLASH) == GLFW_PRESS;
    out.crouch_held =
        glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;

    return out;
}
} // namespace

DesktopRuntimeInputAdapter::DesktopRuntimeInputAdapter(
    DesktopPlatform &platform,
    bool splitscreen_enabled)
    : platform_(platform),
      input_backend_(platform.glfw_window()),
      splitscreen_enabled_(splitscreen_enabled) {}

GameRuntimeInputFrame DesktopRuntimeInputAdapter::poll_input() {
    GameRuntimeInputFrame frame{};
    frame.primary = input_backend_.poll();
    if (splitscreen_enabled_) {
        frame.secondary = poll_secondary_split_input(platform_.glfw_window());
    }
    return frame;
}

void DesktopRuntimeInputAdapter::set_splitscreen_enabled(bool enabled) {
    splitscreen_enabled_ = enabled;
}

DesktopRuntimePlatformAdapter::DesktopRuntimePlatformAdapter(DesktopPlatform &platform)
    : platform_(platform) {}

void DesktopRuntimePlatformAdapter::poll_events() {
    platform_.poll_events();
}

bool DesktopRuntimePlatformAdapter::should_close() const {
    return platform_.should_close();
}

void *DesktopRuntimePlatformAdapter::native_window() {
    return platform_.native_window();
}
