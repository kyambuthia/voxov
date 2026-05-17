#include "platform/sokol/sokol_platform.hpp"

#include "sokol_app.h"

#include <cstring>
#include <cstdio>

// ---------------------------------------------------------------------------
// Per-platform key code mapping (matches GLFW keycodes that input_desktop
// currently uses, so input_desktop.cpp can be ported with minimal changes).
// ---------------------------------------------------------------------------

static int map_sokol_key_to_glfw_code(int sokol_key) {
    // sokol_app key codes map closely to ASCII for printable chars.
    // We define explicit mappings for the keys used in input_desktop.cpp.
    switch (sokol_key) {
    case SAPP_KEYCODE_W:        return 87;   // GLFW_KEY_W
    case SAPP_KEYCODE_A:        return 65;   // GLFW_KEY_A
    case SAPP_KEYCODE_S:        return 83;   // GLFW_KEY_S
    case SAPP_KEYCODE_D:        return 68;   // GLFW_KEY_D
    case SAPP_KEYCODE_SPACE:    return 32;   // GLFW_KEY_SPACE
    case SAPP_KEYCODE_ESCAPE:   return 256;  // GLFW_KEY_ESCAPE
    case SAPP_KEYCODE_UP:       return 265;  // GLFW_KEY_UP
    case SAPP_KEYCODE_DOWN:     return 264;  // GLFW_KEY_DOWN
    case SAPP_KEYCODE_ENTER:    return 257;  // GLFW_KEY_ENTER
    case SAPP_KEYCODE_F:        return 70;   // GLFW_KEY_F
    case SAPP_KEYCODE_E:        return 69;   // GLFW_KEY_E
    case SAPP_KEYCODE_F1:       return 290;  // GLFW_KEY_F1
    case SAPP_KEYCODE_F2:       return 291;  // GLFW_KEY_F2
    case SAPP_KEYCODE_F3:       return 292;  // GLFW_KEY_F3
    case SAPP_KEYCODE_F4:       return 293;  // GLFW_KEY_F4
    case SAPP_KEYCODE_F5:       return 294;  // GLFW_KEY_F5
    case SAPP_KEYCODE_F11:      return 303;  // GLFW_KEY_F11
    case SAPP_KEYCODE_LEFT_SHIFT:
    case SAPP_KEYCODE_RIGHT_SHIFT: return 340; // GLFW_KEY_LEFT_SHIFT
    case SAPP_KEYCODE_LEFT_CONTROL:
    case SAPP_KEYCODE_RIGHT_CONTROL: return 341; // GLFW_KEY_LEFT_CONTROL
    case SAPP_KEYCODE_C:        return 67;   // GLFW_KEY_C
    case SAPP_KEYCODE_Q:        return 81;   // GLFW_KEY_Q
    case SAPP_KEYCODE_R:        return 82;   // GLFW_KEY_R
    case SAPP_KEYCODE_I:        return 73;   // GLFW_KEY_I
    case SAPP_KEYCODE_J:        return 74;   // GLFW_KEY_J
    case SAPP_KEYCODE_K:        return 75;   // GLFW_KEY_K
    case SAPP_KEYCODE_L:        return 76;   // GLFW_KEY_L
    case SAPP_KEYCODE_LEFT:     return 263;  // GLFW_KEY_LEFT
    case SAPP_KEYCODE_RIGHT:    return 262;  // GLFW_KEY_RIGHT
    case SAPP_KEYCODE_SLASH:    return 47;   // GLFW_KEY_SLASH
    default: return -1;
    }
}

// ---------------------------------------------------------------------------
// Key state storage (256 entries covers most used GLFW keycodes).
// ---------------------------------------------------------------------------

static constexpr int kMaxKeys = 512;
static constexpr int kMaxMouseButtons = 8;

static bool g_keys[kMaxKeys] = {};
static bool g_mouse_buttons[kMaxMouseButtons] = {};
static double g_mouse_x = 0.0;
static double g_mouse_y = 0.0;
static float g_mouse_dx = 0.0f;
static float g_mouse_dy = 0.0f;
static bool g_mouse_initialized = false;
static bool g_window_focused = true;

// ---------------------------------------------------------------------------
// DesktopPlatform
// ---------------------------------------------------------------------------

bool DesktopPlatform::init(const PlatformCreateInfo &create_info) {
    /// sokol_app initialization is handled by the global sapp_desc in main.cpp.
    /// This init() stores local state; the actual window is created by sapp_run().
    windowed_width = create_info.width;
    windowed_height = create_info.height;
    fullscreen = create_info.fullscreen;

    // Clear key state
    std::memset(g_keys, 0, sizeof(g_keys));
    std::memset(g_mouse_buttons, 0, sizeof(g_mouse_buttons));
    g_mouse_x = 0.0;
    g_mouse_y = 0.0;
    g_mouse_dx = 0.0f;
    g_mouse_dy = 0.0f;
    g_mouse_initialized = false;

    return true;
}

void DesktopPlatform::shutdown() {
    // sokol_app shutdown is handled by sapp_run() returning.
    std::memset(g_keys, 0, sizeof(g_keys));
    should_close_ = true;
}

void DesktopPlatform::process_event(const sapp_event &event) {
    switch (event.type) {
    case SAPP_EVENTTYPE_KEY_DOWN: {
        const int code = map_sokol_key_to_glfw_code(event.key_code);
        if (code >= 0 && code < kMaxKeys) {
            g_keys[code] = true;
        }
        break;
    }
    case SAPP_EVENTTYPE_KEY_UP: {
        const int code = map_sokol_key_to_glfw_code(event.key_code);
        if (code >= 0 && code < kMaxKeys) {
            g_keys[code] = false;
        }
        break;
    }
    case SAPP_EVENTTYPE_MOUSE_DOWN:
        if (event.mouse_button < kMaxMouseButtons) {
            g_mouse_buttons[event.mouse_button] = true;
        }
        break;
    case SAPP_EVENTTYPE_MOUSE_UP:
        if (event.mouse_button < kMaxMouseButtons) {
            g_mouse_buttons[event.mouse_button] = false;
        }
        break;
    case SAPP_EVENTTYPE_MOUSE_MOVE: {
        const double new_x = event.mouse_x;
        const double new_y = event.mouse_y;
        if (g_mouse_initialized) {
            g_mouse_dx = static_cast<float>(new_x - g_mouse_x);
            g_mouse_dy = static_cast<float>(new_y - g_mouse_y);
        }
        g_mouse_x = new_x;
        g_mouse_y = new_y;
        g_mouse_initialized = true;
        break;
    }
    case SAPP_EVENTTYPE_MOUSE_SCROLL:
        // Scroll not currently used by input_desktop.
        break;
    case SAPP_EVENTTYPE_FOCUSED:
        g_window_focused = true;
        focused_ = true;
        break;
    case SAPP_EVENTTYPE_UNFOCUSED:
        g_window_focused = false;
        focused_ = false;
        break;
    case SAPP_EVENTTYPE_RESIZED:
        windowed_width = event.framebuffer_width;
        windowed_height = event.framebuffer_height;
        break;
    case SAPP_EVENTTYPE_QUIT_REQUESTED:
        should_close_ = true;
        break;
    default:
        break;
    }
}

void DesktopPlatform::on_frame() {
    // Reset per-frame mouse deltas (accumulated during event processing).
    g_mouse_dx = 0.0f;
    g_mouse_dy = 0.0f;
}

bool DesktopPlatform::should_close() const {
    return should_close_;
}

void *DesktopPlatform::native_window() {
    // sokol_app doesn't expose a native window handle easily.
    // Render backend gets the swapchain via sokol_glue instead.
    return nullptr;
}

double DesktopPlatform::now_seconds() const {
    return sapp_frame_duration() * sapp_frame_count();
}

void DesktopPlatform::set_window_title(const char *title) {
    sapp_set_window_title(title);
}

void DesktopPlatform::set_window_size(int width, int height) {
    // sokol_app window resize is not supported at runtime in the same way.
    // This is a no-op for now; the window size is set at creation time.
    (void)width;
    (void)height;
}

void DesktopPlatform::set_fullscreen(bool enabled) {
    // sokol_app fullscreen toggle — note: sokol uses a different API model.
    // For now, this is a simplified toggle.
    sapp_toggle_fullscreen();
    fullscreen = enabled;
}

bool DesktopPlatform::is_fullscreen() const {
    return fullscreen;
}

void DesktopPlatform::toggle_fullscreen() {
    sapp_toggle_fullscreen();
    fullscreen = !fullscreen;
}

// ---- Input state queries (replaces glfwGetKey / glfwGetMouseButton) ----

bool DesktopPlatform::is_key_down(int key_code) const {
    if (key_code < 0 || key_code >= kMaxKeys) return false;
    return g_keys[key_code];
}

bool DesktopPlatform::is_mouse_button_down(int button) const {
    if (button < 0 || button >= kMaxMouseButtons) return false;
    return g_mouse_buttons[button];
}

void DesktopPlatform::mouse_position(double &x, double &y) const {
    x = g_mouse_x;
    y = g_mouse_y;
}

float DesktopPlatform::mouse_delta_x() const {
    return g_mouse_dx;
}

float DesktopPlatform::mouse_delta_y() const {
    return g_mouse_dy;
}

int DesktopPlatform::window_width() const {
    return windowed_width;
}

int DesktopPlatform::window_height() const {
    return windowed_height;
}

bool DesktopPlatform::window_focused() const {
    return g_window_focused;
}
