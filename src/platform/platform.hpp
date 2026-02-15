#pragma once

#include <cstdint>

struct GLFWwindow;

enum class RenderBackendType {
    Vulkan,
    OpenGL
};

struct PlatformCreateInfo {
    const char *title = "VOXOV";
    int width = 1280;
    int height = 720;
    RenderBackendType backend = RenderBackendType::Vulkan;
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

private:
    GLFWwindow *window = nullptr;
};
