#include "engine_core/timing.hpp"
#include "engine_net/net_server.hpp"
#include "game/desktop_runtime_adapter.hpp"
#include "game/game_runtime.hpp"
#include "platform/platform.hpp"
#include "platform/platform_services.hpp"

#include <GLFW/glfw3.h>

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
    bool splitscreen = false;
    bool debug_collision = false;
    bool debug_xray = false;
    bool debug_collision_only = false;
    bool debug_freeze = false;
    bool vehicle_sandbox = false;
    bool spherical_planet = false;
    bool start_fullscreen = false;
    int window_width = 1280;
    int window_height = 720;
    const char *connect_host = nullptr;
    uint16_t connect_port = 7777;
    PhysicsSolverBackend physics_backend = PhysicsSolverBackend::Jolt;

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
            if (std::strcmp(renderer_name, "gl") != 0 &&
                std::strcmp(renderer_name, "opengl") != 0) {
                std::fprintf(stderr,
                             "Ignoring unsupported renderer '%s'; desktop now "
                             "uses OpenGL only.\n",
                             renderer_name);
            }
        } else if (std::strcmp(argv[i], "--physics") == 0 && i + 1 < argc) {
            const char *physics_name = argv[++i];
            if (std::strcmp(physics_name, "avbd") == 0 || std::strcmp(physics_name, "avbd-experimental") == 0) {
                physics_backend = PhysicsSolverBackend::AvbdExperimental;
            } else {
                physics_backend = PhysicsSolverBackend::Jolt;
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
        } else if (std::strcmp(argv[i], "--vehicle-sandbox") == 0) {
            vehicle_sandbox = true;
        } else if (std::strcmp(argv[i], "--spherical-planet") == 0 || std::strcmp(argv[i], "--planet-sphere") == 0) {
            spherical_planet = true;
        } else if (std::strcmp(argv[i], "--flat-world") == 0) {
            spherical_planet = false;
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
    bool server_started = false;
    if (run_server) {
        server_started = server.init(connect_port);
        if (!server_started) {
            std::fprintf(stderr, "Failed to start server on port %u\n", connect_port);
            if (headless_server) {
                return 1;
            }
        }
    }

    if (headless_server) {
        std::fprintf(stderr, "VOXOV headless server started on port %u\n", connect_port);
        while (keep_running.load()) {
            if (server_started) {
                server.pump();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (server_started) {
            server.shutdown();
        }
        return 0;
    }

    DesktopPlatform platform;
    PlatformCreateInfo create_info{};
    create_info.title = "VOXOV";
    create_info.width = (window_width > 0) ? window_width : 1280;
    create_info.height = (window_height > 0) ? window_height : 720;
    create_info.fullscreen = start_fullscreen;
    create_info.backend = RenderBackendType::OpenGL;

    if (!platform.init(create_info)) {
        std::fprintf(stderr, "Platform init failed\n");
        if (server_started) {
            server.shutdown();
        }
        return 1;
    }

    GameRuntime runtime;
    DesktopRuntimePlatformAdapter runtime_platform(platform);
    DesktopRuntimeInputAdapter runtime_input(platform, splitscreen);
    const PlatformServices platform_services = PlatformServices::desktop_default();
    try {
        GameRuntimeInitParams init_params{};
        init_params.platform = RuntimePlatform::Desktop;
        GameRuntimeOptions options{};
        options.devhud = devhud;
        options.noclip = noclip;
        options.splitscreen = splitscreen;
        options.debug_collision = debug_collision;
        options.debug_xray = debug_xray;
        options.debug_collision_only = debug_collision_only;
        options.debug_freeze = debug_freeze;
        options.vehicle_sandbox = vehicle_sandbox;
        options.spherical_planet = spherical_planet;
        options.physics_backend = physics_backend;
        init_params.options = options;
        init_params.input_adapter = &runtime_input;
        init_params.platform_adapter = &runtime_platform;
        init_params.platform_services = &platform_services;
        if (!runtime.init(init_params)) {
            std::fprintf(stderr, "Runtime init failed\n");
            platform.shutdown();
            if (server_started) {
                server.shutdown();
            }
            return 1;
        }
    } catch (const std::exception &e) {
        std::fprintf(stderr, "Runtime init failed: %s\n", e.what());
        platform.shutdown();
        if (server_started) {
            server.shutdown();
        }
        return 1;
    }

    if (connect_host) {
        runtime.connect(connect_host, connect_port);
    }

    FramePacer pacer;
    pacer.init(120.0);
    bool f11_was_down = false;

    while (!runtime.should_close() && keep_running.load()) {
        pacer.begin_frame();
        if (server_started) {
            server.pump();
        }

        runtime.tick(pacer.frame_dt());

        // F11 fullscreen toggle — uses PlatformInputSnapshot
        const bool f11_down = platform.input().keys_down.test(
            static_cast<size_t>(PlatformKey::F11));
        if (f11_down && !f11_was_down) {
            platform.toggle_fullscreen();
        }
        f11_was_down = f11_down;
        pacer.end_frame();

        const RenderStats &stats = runtime.stats();
        char title[128]{};
        std::snprintf(title, sizeof(title),
                      "VOXOV  FPS: %.1f  CPU: %.2fms  NET: %u/%u Bps",
                      stats.fps, stats.cpu_ms, stats.net_tx_bytes_per_sec,
                      stats.net_rx_bytes_per_sec);
        platform.set_title(title);
    }

    runtime.shutdown();
    platform.shutdown();

    if (server_started) {
        server.shutdown();
    }

    return 0;
}
