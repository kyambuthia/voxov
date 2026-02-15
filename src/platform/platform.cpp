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
