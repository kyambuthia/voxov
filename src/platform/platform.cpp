#ifndef VOXOV_PLATFORM_SOKOL

#include "platform/platform.hpp"

#include <GLFW/glfw3.h>

#include <cstring>

// ── GLFW key code → PlatformKey mapping ─────────────────────────────────

static PlatformKey map_glfw_key(int glfw_code) {
    switch (glfw_code) {
    case GLFW_KEY_W:            return PlatformKey::W;
    case GLFW_KEY_A:            return PlatformKey::A;
    case GLFW_KEY_S:            return PlatformKey::S;
    case GLFW_KEY_D:            return PlatformKey::D;
    case GLFW_KEY_I:            return PlatformKey::I;
    case GLFW_KEY_J:            return PlatformKey::J;
    case GLFW_KEY_K:            return PlatformKey::K;
    case GLFW_KEY_L:            return PlatformKey::L;
    case GLFW_KEY_Q:            return PlatformKey::Q;
    case GLFW_KEY_R:            return PlatformKey::R;
    case GLFW_KEY_E:            return PlatformKey::E;
    case GLFW_KEY_F:            return PlatformKey::F;
    case GLFW_KEY_C:            return PlatformKey::C;
    case GLFW_KEY_SPACE:        return PlatformKey::Space;
    case GLFW_KEY_ESCAPE:       return PlatformKey::Escape;
    case GLFW_KEY_ENTER:        return PlatformKey::Enter;
    case GLFW_KEY_TAB:          return PlatformKey::Tab;
    case GLFW_KEY_UP:           return PlatformKey::Up;
    case GLFW_KEY_DOWN:         return PlatformKey::Down;
    case GLFW_KEY_LEFT:         return PlatformKey::Left;
    case GLFW_KEY_RIGHT:        return PlatformKey::Right;
    case GLFW_KEY_F1:           return PlatformKey::F1;
    case GLFW_KEY_F2:           return PlatformKey::F2;
    case GLFW_KEY_F3:           return PlatformKey::F3;
    case GLFW_KEY_F4:           return PlatformKey::F4;
    case GLFW_KEY_F5:           return PlatformKey::F5;
    case GLFW_KEY_F11:          return PlatformKey::F11;
    case GLFW_KEY_LEFT_SHIFT:   return PlatformKey::LeftShift;
    case GLFW_KEY_RIGHT_SHIFT:  return PlatformKey::RightShift;
    case GLFW_KEY_LEFT_CONTROL: return PlatformKey::LeftControl;
    case GLFW_KEY_RIGHT_CONTROL:return PlatformKey::RightControl;
    case GLFW_KEY_SLASH:        return PlatformKey::Slash;
    default: return PlatformKey::Unknown;
    }
}

// ── DesktopPlatform (GLFW backend) ──────────────────────────────────────

bool DesktopPlatform::init(const PlatformCreateInfo &create_info) {
    if (!glfwInit()) {
        return false;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif

    window = glfwCreateWindow(create_info.width, create_info.height, create_info.title, nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return false;
    }

    windowed_width = create_info.width;
    windowed_height = create_info.height;
    glfwGetWindowPos(window, &windowed_x, &windowed_y);
    fullscreen_ = false;

    if (create_info.fullscreen) {
        set_fullscreen(true);
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    input_ = {};
    refresh_input_snapshot();

    return true;
}

void DesktopPlatform::shutdown() {
    if (window) {
        glfwDestroyWindow(window);
        window = nullptr;
    }
    glfwTerminate();
}

void DesktopPlatform::poll_events() {
    glfwPollEvents();
    refresh_input_snapshot();
}

void DesktopPlatform::refresh_input_snapshot() {
    if (!window) return;

    // Keys
    static constexpr int kKnownKeys[] = {
        GLFW_KEY_W, GLFW_KEY_A, GLFW_KEY_S, GLFW_KEY_D,
        GLFW_KEY_I, GLFW_KEY_J, GLFW_KEY_K, GLFW_KEY_L,
        GLFW_KEY_Q, GLFW_KEY_R, GLFW_KEY_E, GLFW_KEY_F,
        GLFW_KEY_C,
        GLFW_KEY_SPACE, GLFW_KEY_ESCAPE, GLFW_KEY_ENTER, GLFW_KEY_TAB,
        GLFW_KEY_UP, GLFW_KEY_DOWN, GLFW_KEY_LEFT, GLFW_KEY_RIGHT,
        GLFW_KEY_F1, GLFW_KEY_F2, GLFW_KEY_F3, GLFW_KEY_F4, GLFW_KEY_F5,
        GLFW_KEY_F11,
        GLFW_KEY_LEFT_SHIFT, GLFW_KEY_RIGHT_SHIFT,
        GLFW_KEY_LEFT_CONTROL, GLFW_KEY_RIGHT_CONTROL,
        GLFW_KEY_SLASH,
    };
    for (int code : kKnownKeys) {
        const PlatformKey pk = map_glfw_key(code);
        if (pk != PlatformKey::Unknown) {
            if (glfwGetKey(window, code) == GLFW_PRESS)
                input_.keys_down.set(static_cast<size_t>(pk));
            else
                input_.keys_down.reset(static_cast<size_t>(pk));
        }
    }

    // Mouse buttons
    for (int b = 0; b < 8; ++b) {
        if (glfwGetMouseButton(window, b) == GLFW_PRESS)
            input_.mouse_down.set(static_cast<size_t>(b));
        else
            input_.mouse_down.reset(static_cast<size_t>(b));
    }

    // Mouse position / deltas
    double mx = 0.0, my = 0.0;
    glfwGetCursorPos(window, &mx, &my);
    const float prev_x = input_.mouse_pos.x;
    const float prev_y = input_.mouse_pos.y;
    input_.mouse_pos.x = static_cast<float>(mx);
    input_.mouse_pos.y = static_cast<float>(my);
    input_.mouse_delta.x = static_cast<float>(mx) - prev_x;
    input_.mouse_delta.y = static_cast<float>(my) - prev_y;

    // Window focus
    input_.focused = glfwGetWindowAttrib(window, GLFW_FOCUSED) == GLFW_TRUE;
}

// ── PlatformRuntime interface ──────────────────────────────────────────

RenderSurface DesktopPlatform::surface() const {
    int w = 1, h = 1;
    if (window) glfwGetFramebufferSize(window, &w, &h);
    float dpi = 1.0f;
    if (window) {
        int sw = 0, sh = 0;
        glfwGetWindowSize(window, &sw, &sh);
        if (sw > 0) dpi = static_cast<float>(w) / static_cast<float>(sw);
    }
    return { w, h, dpi };
}

void DesktopPlatform::set_title(const char *title) {
    if (window) glfwSetWindowTitle(window, title);
}

void DesktopPlatform::set_fullscreen(bool enabled) {
    if (!window || fullscreen_ == enabled) return;

    if (enabled) {
        glfwGetWindowPos(window, &windowed_x, &windowed_y);
        glfwGetWindowSize(window, &windowed_width, &windowed_height);
        GLFWmonitor *monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode *mode = monitor ? glfwGetVideoMode(monitor) : nullptr;
        if (!monitor || !mode) return;
        glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
        fullscreen_ = true;
    } else {
        const int rw = (windowed_width > 0) ? windowed_width : 1280;
        const int rh = (windowed_height > 0) ? windowed_height : 720;
        glfwSetWindowMonitor(window, nullptr, windowed_x, windowed_y, rw, rh, 0);
        fullscreen_ = false;
    }
}

void DesktopPlatform::toggle_fullscreen() {
    set_fullscreen(!fullscreen_);
}

bool DesktopPlatform::should_close() const {
    return !window || glfwWindowShouldClose(window);
}

// ── GLFW-specific accessors ─────────────────────────────────────────────

void *DesktopPlatform::native_window() {
    return window;
}

GLFWwindow *DesktopPlatform::glfw_window() {
    return window;
}

// ── Legacy query helpers (bridge for incremental migration) ─────────────

bool DesktopPlatform::is_key_down(int key_code) const {
    return window && glfwGetKey(window, key_code) == GLFW_PRESS;
}

bool DesktopPlatform::is_mouse_button_down(int button) const {
    return window && glfwGetMouseButton(window, button) == GLFW_PRESS;
}

void DesktopPlatform::mouse_position(double &x, double &y) const {
    if (!window) { x = 0.0; y = 0.0; return; }
    glfwGetCursorPos(window, &x, &y);
}

float DesktopPlatform::mouse_delta_x() const {
    return input_.mouse_delta.x;
}

float DesktopPlatform::mouse_delta_y() const {
    return input_.mouse_delta.y;
}

int DesktopPlatform::window_width() const {
    int w = 1, h = 1;
    if (window) glfwGetFramebufferSize(window, &w, &h);
    return w;
}

int DesktopPlatform::window_height() const {
    int w = 1, h = 1;
    if (window) glfwGetFramebufferSize(window, &w, &h);
    return h;
}

bool DesktopPlatform::window_focused() const {
    return input_.focused;
}

#endif // VOXOV_PLATFORM_SOKOL
