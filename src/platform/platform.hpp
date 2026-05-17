#pragma once

#include <cstdint>

#include "engine_render/render_backend_type.hpp"
#include "platform/platform_input_state.hpp"

// ---------------------------------------------------------------------------
// Unified platform header — delegates to the active backend.
// When VOXOV_PLATFORM_SOKOL is defined, the sokol_app backend is used.
// Otherwise, GLFW is the fallback for incremental migration.
// ---------------------------------------------------------------------------

#ifdef VOXOV_PLATFORM_SOKOL
#include "platform/sokol/sokol_platform.hpp"
#else
// Legacy GLFW path — kept for incremental migration.
struct GLFWwindow;

struct PlatformCreateInfo {
    const char *title = "VOXOV";
    int width = 1280;
    int height = 720;
    bool fullscreen = false;
    RenderBackendType backend = RenderBackendType::OpenGL;
};

class DesktopPlatform : public PlatformRuntime {
public:
    bool init(const PlatformCreateInfo &create_info);
    void shutdown();

    void poll_events();

    // ── PlatformRuntime interface ────────────────────────────────────
    const PlatformInputSnapshot &input() const override { return input_; }
    RenderSurface surface() const override;
    void set_title(const char *title) override;
    void set_fullscreen(bool enabled) override;
    bool is_fullscreen() const override { return fullscreen_; }
    void toggle_fullscreen() override;
    bool should_close() const override;

    // ── GLFW-specific (legacy) ───────────────────────────────────────
    void *native_window();
    GLFWwindow *glfw_window();

    // ── Legacy query helpers ─────────────────────────────────────────
    // Prefer platform.input() for new code.
    bool is_key_down(int key_code) const;
    bool is_mouse_button_down(int button) const;
    void mouse_position(double &x, double &y) const;
    float mouse_delta_x() const;
    float mouse_delta_y() const;
    int window_width() const;
    int window_height() const;
    bool window_focused() const;

private:
    void refresh_input_snapshot();

    GLFWwindow *window = nullptr;
    PlatformInputSnapshot input_{};
    bool fullscreen_ = false;
    int windowed_x = 100;
    int windowed_y = 100;
    int windowed_width = 1280;
    int windowed_height = 720;
};
#endif
