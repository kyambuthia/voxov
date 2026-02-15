#include "engine/engine.hpp"
#include "engine_core/timing.hpp"
#include "engine_input/input_state.hpp"
#include "engine_net/net_server.hpp"
#include "platform/platform.hpp"
#include "platform/desktop/input_desktop.hpp"

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
}

int main(int argc, char **argv) {
    bool run_server = false;
    bool headless_server = false;
    bool devhud = false;
    bool noclip = false;
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
    create_info.width = 1280;
    create_info.height = 720;
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

    while (!platform.should_close() && keep_running.load()) {
        pacer.begin_frame();
        platform.poll_events();

        const InputState input = desktop_input.poll();
        engine.set_input(input, false);
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
