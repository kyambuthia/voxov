#include "engine/engine.hpp"
#include "engine_core/timing.hpp"
#include "engine_net/net_server.hpp"

#include <GLFW/glfw3.h>
#include <cstring>

int main(int argc, char **argv) {
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

    if (!glfwInit()) {
        return 1;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow *window = glfwCreateWindow(1280, 720, "Voxov Game", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }

    Engine engine;
    engine.init(window);
    if (connect_host) {
        engine.connect(connect_host, connect_port);
    }

    NetServer server;
    if (run_server) {
        server.init(connect_port);
    }

    FramePacer pacer;
    pacer.init(120.0);

    while (!glfwWindowShouldClose(window)) {
        pacer.begin_frame();
        glfwPollEvents();
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
    return 0;
}
