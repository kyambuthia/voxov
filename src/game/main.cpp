#include "engine/engine.hpp"
#include "engine_core/timing.hpp"
#include "engine_input/input_state.hpp"
#include "engine_net/net_server.hpp"
#include "platform/platform.hpp"
#include "platform/desktop/input_desktop.hpp"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <exception>
#include <thread>

namespace {
std::atomic<bool> keep_running{true};

void on_signal(int) {
    keep_running = false;
}

InputState poll_secondary_split_input(GLFWwindow *window) {
    InputState out{};
    if (!window) {
        return out;
    }

    if (glfwGetKey(window, GLFW_KEY_I) == GLFW_PRESS) {
        out.move.y += 1.0f;
    }
    if (glfwGetKey(window, GLFW_KEY_K) == GLFW_PRESS) {
        out.move.y -= 1.0f;
    }
    if (glfwGetKey(window, GLFW_KEY_L) == GLFW_PRESS) {
        out.move.x += 1.0f;
    }
    if (glfwGetKey(window, GLFW_KEY_J) == GLFW_PRESS) {
        out.move.x -= 1.0f;
    }
    if (out.move.x != 0.0f || out.move.y != 0.0f) {
        out.move = glm::normalize(out.move);
    }

    constexpr float look_speed = 5.0f;
    if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS) {
        out.look_delta.x -= look_speed;
    }
    if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) {
        out.look_delta.x += look_speed;
    }
    if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS) {
        out.look_delta.y -= look_speed;
    }
    if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS) {
        out.look_delta.y += look_speed;
    }

    const bool rctrl_down =
        glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS ||
        glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS;
    out.jump_held = rctrl_down;
    out.jump_pressed = rctrl_down;
    out.sprint_held =
        glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS ||
        glfwGetKey(window, GLFW_KEY_SLASH) == GLFW_PRESS;

    return out;
}
}

int main(int argc, char **argv) {
    bool run_server = false;
    bool headless_server = false;
    bool devhud = false;
    bool noclip = false;
    bool splitscreen = false;
    bool debug_collision = false;
    bool debug_xray = false;
    bool debug_collision_only = false;
    bool debug_freeze = false;
    bool start_fullscreen = false;
    int window_width = 1280;
    int window_height = 720;
    const char *connect_host = nullptr;
    uint16_t connect_port = 7777;
    RenderBackendType backend = RenderBackendType::Vulkan;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--server") == 0) {
            run_server = true;
        } else if (std::strcmp(argv[i], "--headless-server") == 0) {
            run_server = true;
            headless_server = true;
        } else if (std::strcmp(argv[i], "--connect") == 0 && i + 1 < argc) {
            connect_host = argv[++i];
        } else if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            connect_port = static_cast<uint16_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (std::strcmp(argv[i], "--renderer") == 0 && i + 1 < argc) {
            const char *renderer_name = argv[++i];
            if (std::strcmp(renderer_name, "gl") == 0 || std::strcmp(renderer_name, "opengl") == 0) {
                backend = RenderBackendType::OpenGL;
            } else {
                backend = RenderBackendType::Vulkan;
            }
        } else if (std::strcmp(argv[i], "--devhud") == 0) {
            devhud = true;
        } else if (std::strcmp(argv[i], "--noclip") == 0) {
            noclip = true;
        } else if (std::strcmp(argv[i], "--splitscreen") == 0) {
            splitscreen = true;
        } else if (std::strcmp(argv[i], "--debug-collision") == 0) {
            debug_collision = true;
        } else if (std::strcmp(argv[i], "--debug-xray") == 0) {
            debug_collision = true;
            debug_xray = true;
        } else if (std::strcmp(argv[i], "--debug-collision-only") == 0) {
            debug_collision = true;
            debug_collision_only = true;
        } else if (std::strcmp(argv[i], "--debug-freeze") == 0) {
            debug_collision = true;
            debug_freeze = true;
        } else if (std::strcmp(argv[i], "--fullscreen") == 0) {
            start_fullscreen = true;
        } else if (std::strcmp(argv[i], "--windowed") == 0) {
            start_fullscreen = false;
        } else if (std::strcmp(argv[i], "--width") == 0 && i + 1 < argc) {
            window_width = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--height") == 0 && i + 1 < argc) {
            window_height = std::atoi(argv[++i]);
        }
    }

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    NetServer server;
    if (run_server) {
        server.init(connect_port);
    }

    if (headless_server) {
        std::fprintf(stderr, "VOXOV headless server started on port %u\n", connect_port);
        while (keep_running.load()) {
            server.pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        server.shutdown();
        return 0;
    }

    DesktopPlatform platform;
    PlatformCreateInfo create_info{};
    create_info.title = "VOXOV";
    create_info.width = (window_width > 0) ? window_width : 1280;
    create_info.height = (window_height > 0) ? window_height : 720;
    create_info.fullscreen = start_fullscreen;
    create_info.backend = backend;

    if (!platform.init(create_info)) {
        std::fprintf(stderr, "Platform init failed\n");
        if (run_server) {
            server.shutdown();
        }
        return 1;
    }

    Engine engine;
    try {
        EngineRuntimeOptions options{};
        options.devhud = devhud;
        options.noclip = noclip;
        options.splitscreen = splitscreen;
        options.debug_collision = debug_collision;
        options.debug_xray = debug_xray;
        options.debug_collision_only = debug_collision_only;
        options.debug_freeze = debug_freeze;
        engine.init(platform.native_window(), backend, options);
    } catch (const std::exception &e) {
        std::fprintf(stderr, "Engine init failed: %s\n", e.what());
        platform.shutdown();
        if (run_server) {
            server.shutdown();
        }
        return 1;
    }

    if (connect_host) {
        engine.connect(connect_host, connect_port);
    }

    FramePacer pacer;
    pacer.init(120.0);
    DesktopInputBackend desktop_input(platform.glfw_window());
    bool f11_was_down = false;

    while (!platform.should_close() && keep_running.load()) {
        pacer.begin_frame();
        platform.poll_events();

        const bool f11_down = glfwGetKey(platform.glfw_window(), GLFW_KEY_F11) == GLFW_PRESS;
        if (f11_down && !f11_was_down) {
            platform.toggle_fullscreen();
        }
        f11_was_down = f11_down;

        const InputState input = desktop_input.poll();
        const InputState input_secondary = splitscreen ? poll_secondary_split_input(platform.glfw_window()) : InputState{};
        engine.set_input(input, input_secondary, false);
        if (run_server) {
            server.pump();
        }

        engine.tick(pacer.frame_dt());
        pacer.end_frame();

        const RenderStats &stats = engine.stats();
        char title[128]{};
        std::snprintf(title, sizeof(title), "VOXOV  FPS: %.1f  CPU: %.2fms", stats.fps, stats.cpu_ms);
        platform.set_window_title(title);
    }

    engine.shutdown();
    platform.shutdown();

    if (run_server) {
        server.shutdown();
    }

    return 0;
}
