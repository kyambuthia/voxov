#pragma once

#include "engine_render/render_backend_type.hpp"

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
class DesktopPlatform {
public:
    bool init(const PlatformCreateInfo &create_info);
    void shutdown();

    /// sokol_app uses callbacks — the platform processes the event
    /// and stores derived state (keys, mouse, window flags).
    void process_event(const sapp_event &event);
    void on_frame();

    bool should_close() const;
    void *native_window();

    double now_seconds() const;
    void set_window_title(const char *title);
    void set_window_size(int width, int height);
    void set_fullscreen(bool enabled);
    bool is_fullscreen() const;
    void toggle_fullscreen();

    // Input state queried by the input backend.
    bool is_key_down(int key_code) const;
    bool is_mouse_button_down(int button) const;
    void mouse_position(double &x, double &y) const;
    float mouse_delta_x() const;
    float mouse_delta_y() const;
    int window_width() const;
    int window_height() const;
    bool window_focused() const;

private:
    bool fullscreen = false;
    int windowed_x = 100;
    int windowed_y = 100;
    int windowed_width = 1280;
    int windowed_height = 720;
    bool should_close_ = false;
    bool focused_ = true;
    double mouse_x_ = 0.0;
    double mouse_y_ = 0.0;
    float mouse_dx_ = 0.0f;
    float mouse_dy_ = 0.0f;
};

/// Global accessor used by sokol_app callbacks (which are C function pointers
/// and cannot capture a this-pointer directly).
using AppUserData = DesktopPlatform;
