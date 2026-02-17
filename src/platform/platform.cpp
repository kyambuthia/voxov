#include "platform/platform.hpp"

#include <GLFW/glfw3.h>

bool DesktopPlatform::init(const PlatformCreateInfo &create_info) {
    if (!glfwInit()) {
        return false;
    }

    if (create_info.backend == RenderBackendType::Vulkan) {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    } else {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#ifdef __APPLE__
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
    }

    window = glfwCreateWindow(create_info.width, create_info.height, create_info.title, nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return false;
    }

    windowed_width = create_info.width;
    windowed_height = create_info.height;
    glfwGetWindowPos(window, &windowed_x, &windowed_y);
    fullscreen = false;

    if (create_info.fullscreen) {
        set_fullscreen(true);
    }

    if (create_info.backend == RenderBackendType::OpenGL) {
        glfwMakeContextCurrent(window);
        glfwSwapInterval(1);
    }

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
}

bool DesktopPlatform::should_close() const {
    return !window || glfwWindowShouldClose(window);
}

void *DesktopPlatform::native_window() {
    return window;
}

GLFWwindow *DesktopPlatform::glfw_window() {
    return window;
}

double DesktopPlatform::now_seconds() const {
    return glfwGetTime();
}

void DesktopPlatform::set_window_title(const char *title) {
    if (window) {
        glfwSetWindowTitle(window, title);
    }
}

void DesktopPlatform::set_window_size(int width, int height) {
    if (!window) {
        return;
    }
    const int clamped_w = (width > 0) ? width : 1;
    const int clamped_h = (height > 0) ? height : 1;
    if (!fullscreen) {
        windowed_width = clamped_w;
        windowed_height = clamped_h;
    }
    glfwSetWindowSize(window, clamped_w, clamped_h);
}

void DesktopPlatform::set_fullscreen(bool enabled) {
    if (!window || fullscreen == enabled) {
        return;
    }

    if (enabled) {
        glfwGetWindowPos(window, &windowed_x, &windowed_y);
        glfwGetWindowSize(window, &windowed_width, &windowed_height);
        GLFWmonitor *monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode *mode = monitor ? glfwGetVideoMode(monitor) : nullptr;
        if (!monitor || !mode) {
            return;
        }
        glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
        fullscreen = true;
        return;
    }

    const int restore_w = (windowed_width > 0) ? windowed_width : 1280;
    const int restore_h = (windowed_height > 0) ? windowed_height : 720;
    glfwSetWindowMonitor(window, nullptr, windowed_x, windowed_y, restore_w, restore_h, 0);
    fullscreen = false;
}

bool DesktopPlatform::is_fullscreen() const {
    return fullscreen;
}

void DesktopPlatform::toggle_fullscreen() {
    set_fullscreen(!fullscreen);
}
