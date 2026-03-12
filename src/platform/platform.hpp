#pragma once

#include <cstdint>

struct GLFWwindow;

enum class RenderBackendType {
    OpenGL
};

struct PlatformCreateInfo {
    const char *title = "VOXOV";
    int width = 1280;
    int height = 720;
    bool fullscreen = false;
    RenderBackendType backend = RenderBackendType::OpenGL;
};

class DesktopPlatform {
public:
    bool init(const PlatformCreateInfo &create_info);
    void shutdown();
    void poll_events();
    bool should_close() const;

    void *native_window();
    GLFWwindow *glfw_window();

    double now_seconds() const;
    void set_window_title(const char *title);
    void set_window_size(int width, int height);
    void set_fullscreen(bool enabled);
    bool is_fullscreen() const;
    void toggle_fullscreen();

private:
    GLFWwindow *window = nullptr;
    bool fullscreen = false;
    int windowed_x = 100;
    int windowed_y = 100;
    int windowed_width = 1280;
    int windowed_height = 720;
};
