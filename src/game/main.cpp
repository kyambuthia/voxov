#include "engine/engine.hpp"
#include "engine_core/timing.hpp"
#include "engine_net/net_server.hpp"

#include <GLFW/glfw3.h>
#include <cstring>
#include <cstdio>
#include <exception>
#include <cstdlib>

static void glfw_error_callback(int error, const char *description) {
    std::fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

static void glfw_close_callback(GLFWwindow *window) {
    (void)window;
    std::fprintf(stderr, "GLFW window close requested\n");
}

int main(int argc, char **argv) {
    std::fprintf(stderr, "Voxov starting...\n");
    const char *display = std::getenv("DISPLAY");
    const char *wayland = std::getenv("WAYLAND_DISPLAY");
    std::fprintf(stderr, "DISPLAY=%s WAYLAND_DISPLAY=%s\n",
                 display ? display : "(unset)",
                 wayland ? wayland : "(unset)");
    bool run_server = false;
    const char *connect_host = nullptr;
    uint16_t connect_port = 7777;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--server") == 0) {
            run_server = true;
        } else if (std::strcmp(argv[i], "--connect") == 0 && i + 1 < argc) {
            connect_host = argv[i + 1];
            i++;
        }
    }

    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        std::fprintf(stderr, "GLFW init failed\n");
        return 1;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow *window = glfwCreateWindow(1280, 720, "Voxov Game", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "GLFW window creation failed\n");
        glfwTerminate();
        return 1;
    }
    glfwSetWindowCloseCallback(window, glfw_close_callback);
    std::fprintf(stderr, "GLFW window created\n");

    Engine engine;
    try {
        engine.init(window);
    } catch (const std::exception &e) {
        std::fprintf(stderr, "Engine init failed: %s\n", e.what());
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    if (connect_host) {
        engine.connect(connect_host, connect_port);
    }

    NetServer server;
    if (run_server) {
        server.init(connect_port);
    }

    FramePacer pacer;
    pacer.init(120.0);

    if (glfwWindowShouldClose(window)) {
        std::fprintf(stderr, "GLFW window closed immediately after creation\n");
        engine.shutdown();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    double start_time = glfwGetTime();
    while (!glfwWindowShouldClose(window)) {
        pacer.begin_frame();
        glfwPollEvents();
        if (glfwWindowShouldClose(window)) {
            double elapsed = glfwGetTime() - start_time;
            if (elapsed < 1.0) {
                std::fprintf(stderr, "Ignoring early close request (elapsed=%.2f)\n", elapsed);
                glfwSetWindowShouldClose(window, GLFW_FALSE);
            }
        }
        float move_x = 0.0f;
        float move_y = 0.0f;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
            move_x -= 1.0f;
        }
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
            move_x += 1.0f;
        }
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
            move_y += 1.0f;
        }
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
            move_y -= 1.0f;
        }
        engine.set_input(move_x, move_y);
        if (run_server) {
            server.pump();
        }
        engine.tick(pacer.frame_dt());
        pacer.end_frame();
    }

    engine.shutdown();
    if (run_server) {
        server.shutdown();
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    std::fprintf(stderr, "Voxov shutdown complete\n");
    return 0;
}
