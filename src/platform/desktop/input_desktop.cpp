#include "platform/desktop/input_desktop.hpp"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

DesktopInputBackend::DesktopInputBackend(GLFWwindow *window_handle)
    : window(window_handle) {}

InputState DesktopInputBackend::poll() {
    InputState out{};

    if (!window) {
        return out;
    }

    const bool rmb_down = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    if (rmb_down && !prev_rmb_down) {
        look_mode = !look_mode;
        glfwSetInputMode(window, GLFW_CURSOR, look_mode ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        mouse_initialized = false;
    }
    prev_rmb_down = rmb_down;

    out.look_mode = look_mode;

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

    double x = 0.0;
    double y = 0.0;
    glfwGetCursorPos(window, &x, &y);
    if (!mouse_initialized) {
        prev_mouse_x = x;
        prev_mouse_y = y;
        mouse_initialized = true;
    }

    if (look_mode) {
        out.look_delta.x = static_cast<float>(x - prev_mouse_x);
        out.look_delta.y = static_cast<float>(y - prev_mouse_y);
    }

    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) {
        out.zoom_delta += 0.08f;
    }
    if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) {
        out.zoom_delta -= 0.08f;
    }

    prev_mouse_x = x;
    prev_mouse_y = y;

    return out;
}
