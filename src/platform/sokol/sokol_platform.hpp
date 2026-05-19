#pragma once

#include "engine_render/render_backend_type.hpp"
#include "platform/platform_input_state.hpp"

// Forward declarations for sokol types (real types come from sokol_app.h
// in the .cpp — we keep headers clean of platform-specific includes).
struct sapp_event;

struct PlatformCreateInfo {
    const char *title = "VOXOV";
    int width = 1280;
    int height = 720;
    bool fullscreen = false;
    RenderBackendType backend = RenderBackendType::Sokol;
};

/// Thin platform abstraction over sokol_app.
/// The sokol event callback feeds input state into the platform so
/// downstream code never calls native APIs directly.
class DesktopPlatform : public PlatformRuntime {
public:
    bool init(const PlatformCreateInfo &create_info);
    void shutdown();

    /// sokol_app uses callbacks — the platform processes the event
    /// and stores derived state (keys, mouse, window flags).
    void process_event(const sapp_event &event);
    void on_frame();

    // ── PlatformRuntime interface ────────────────────────────────────
    const PlatformInputSnapshot &input() const override { return input_; }
    RenderSurface surface() const override;
    void set_title(const char *title) override;
    void set_fullscreen(bool enabled) override;
    bool is_fullscreen() const override { return fullscreen_; }
    void toggle_fullscreen() override;
    bool should_close() const override;
    void set_mouse_lock(bool enabled);

    // ── Legacy query helpers (kept for incremental migration) ────────
    // Prefer platform.input() for new code.
    bool is_key_down(int key_code) const;
    bool is_mouse_button_down(int button) const;
    void mouse_position(double &x, double &y) const;
    float mouse_delta_x() const;
    float mouse_delta_y() const;

private:
    PlatformInputSnapshot input_{};
    bool fullscreen_ = false;
    bool should_close_ = false;
    bool mouse_locked_ = false;
};

/// Global accessor used by sokol_app callbacks (which are C function pointers
/// and cannot capture a this-pointer directly).
using AppUserData = DesktopPlatform;
