/// Voxov SDL3 desktop entry point.

#include "engine_core/timing.hpp"
#include "engine_net/net_server.hpp"
#include "game/desktop_runtime_adapter.hpp"
#include "game/game_runtime.hpp"
#include "platform/platform.hpp"
#include "platform/platform_services.hpp"

#include <spdlog/cfg/env.h>
#include <spdlog/spdlog.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <thread>

namespace {

struct AppOptions {
    bool run_server = false;
    int window_width = 1280;
    int window_height = 720;
    bool start_fullscreen = false;
    const char *connect_host = nullptr;
    uint16_t connect_port = 7777;
    PhysicsSolverBackend physics_backend = PhysicsSolverBackend::AvbdExperimental;
};

std::atomic<bool> g_keep_running{true};

} // namespace

int main(int argc, char **argv) {
    spdlog::cfg::load_env_levels();

    AppOptions opts{};
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--server") == 0) {
            opts.run_server = true;
        } else if (std::strcmp(argv[i], "--headless-server") == 0) {
            uint16_t port = 7777;
            for (int j = i + 1; j < argc; ++j) {
                if (std::strcmp(argv[j], "--port") == 0 && j + 1 < argc) {
                    port = static_cast<uint16_t>(std::strtoul(argv[++j], nullptr, 10));
                    break;
                }
            }
            NetServer server;
            if (!server.init(port)) {
                std::fprintf(stderr, "Failed to start headless server on port %u\n", port);
                return 1;
            }
            std::fprintf(stderr, "VOXOV headless server started on port %u\n", port);
            std::signal(SIGINT, [](int) { g_keep_running = false; });
            std::signal(SIGTERM, [](int) { g_keep_running = false; });
            while (g_keep_running.load()) {
                server.pump();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            server.shutdown();
            return 0;
        } else if (std::strcmp(argv[i], "--connect") == 0 && i + 1 < argc) {
            opts.connect_host = argv[++i];
        } else if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            opts.connect_port = static_cast<uint16_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (std::strcmp(argv[i], "--physics") == 0 && i + 1 < argc) {
            if (std::strcmp(argv[++i], "avbd") == 0 ||
                std::strcmp(argv[i], "avbd-experimental") == 0) {
                opts.physics_backend = PhysicsSolverBackend::AvbdExperimental;
            }
        } else if (std::strcmp(argv[i], "--fullscreen") == 0) {
            opts.start_fullscreen = true;
        } else if (std::strcmp(argv[i], "--windowed") == 0) {
            opts.start_fullscreen = false;
        } else if (std::strcmp(argv[i], "--width") == 0 && i + 1 < argc) {
            opts.window_width = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--height") == 0 && i + 1 < argc) {
            opts.window_height = std::atoi(argv[++i]);
        }
    }

    std::signal(SIGINT, [](int) { g_keep_running = false; });
    std::signal(SIGTERM, [](int) { g_keep_running = false; });

    DesktopPlatform platform;
    const PlatformCreateInfo create_info{
        .title = "VOXOV",
        .width = opts.window_width,
        .height = opts.window_height,
        .fullscreen = opts.start_fullscreen,
        .backend = RenderBackendType::Sokol,
    };
    if (!platform.init(create_info)) {
        spdlog::error("SDL3 platform init failed");
        return 1;
    }
    spdlog::info("voxov_init — SDL3 window ready");

    DesktopRuntimePlatformAdapter runtime_platform(platform);
    DesktopRuntimeInputAdapter runtime_input(platform, false);
    const PlatformServices platform_services = PlatformServices::desktop_default();

    GameRuntime runtime;
    GameRuntimeInitParams init_params{};
    init_params.platform = RuntimePlatform::Desktop;
    init_params.options.physics_backend = opts.physics_backend;
    init_params.options.render_backend = RenderBackendType::Sokol;
    init_params.input_adapter = &runtime_input;
    init_params.platform_adapter = &runtime_platform;
    init_params.platform_services = &platform_services;

    if (!runtime.init(init_params)) {
        spdlog::error("Runtime init failed");
        platform.shutdown();
        return 1;
    }

    if (opts.connect_host) {
        runtime.connect(opts.connect_host, opts.connect_port);
    }

    FramePacer pacer;
    pacer.init(120.0);

    NetServer server;
    bool server_started = false;
    if (opts.run_server) {
        server_started = server.init(opts.connect_port);
    }

    auto take_screenshot = [&](const char *path) {
        const std::filesystem::path parent =
            std::filesystem::path(path).parent_path();
        if (!parent.empty()) {
            std::error_code directory_error;
            std::filesystem::create_directories(parent, directory_error);
        }
        const RenderSurface surf = platform.surface();
        if (runtime.capture_screenshot(path, surf.width, surf.height)) {
            spdlog::info("Screenshot saved: {}", path);
        }
    };

    uint32_t automated_capture_frame = 0;
    bool f5_was_down = false;
    bool f12_was_down = false;
    bool f11_was_down = false;

    while (g_keep_running.load() && !platform.should_close() &&
           !runtime.should_close()) {
        pacer.begin_frame();
        platform.poll_events();

        if (server_started) {
            server.pump();
        }

        runtime.tick(pacer.frame_dt());
        platform.swap_buffers();

        if (const char *path = std::getenv("VOXOV_AUTO_SCREENSHOT")) {
            static const uint32_t capture_at = [] {
                const char *frame = std::getenv("VOXOV_AUTO_SCREENSHOT_FRAME");
                const long parsed =
                    frame != nullptr ? std::strtol(frame, nullptr, 10) : 0;
                return parsed > 0 ? static_cast<uint32_t>(parsed) : 220u;
            }();
            ++automated_capture_frame;
            if (automated_capture_frame == capture_at) {
                take_screenshot(path);
                break;
            }
        }

        const bool f5_down = platform.input().keys_down.test(
            static_cast<size_t>(PlatformKey::F5));
        if (f5_down && !f5_was_down) {
            take_screenshot("screenshots/debug_screenshot.png");
        }
        f5_was_down = f5_down;

        const bool f12_down = platform.input().keys_down.test(
            static_cast<size_t>(PlatformKey::F12));
        if (f12_down && !f12_was_down) {
            std::error_code directory_error;
            std::filesystem::create_directories("screenshots", directory_error);
            char filepath[256]{};
            const auto now = std::chrono::system_clock::now();
            const auto t = std::chrono::system_clock::to_time_t(now);
            std::tm tm{};
            if (const std::tm *local_tm = std::localtime(&t)) {
                tm = *local_tm;
            }
            std::snprintf(filepath, sizeof(filepath),
                          "screenshots/voxov_%04d%02d%02d_%02d%02d%02d.png",
                          tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                          tm.tm_hour, tm.tm_min, tm.tm_sec);
            const RenderSurface surf = platform.surface();
            if (runtime.capture_screenshot(filepath, surf.width, surf.height)) {
                spdlog::info("Screenshot saved: {}", filepath);
            } else {
                spdlog::warn("Screenshot failed: {}", filepath);
            }
        }
        f12_was_down = f12_down;

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
                      "VOXOV (SDL3)  FPS: %.1f  CPU: %.2fms  NET: %u/%u Bps",
                      stats.fps, stats.cpu_ms, stats.net_tx_bytes_per_sec,
                      stats.net_rx_bytes_per_sec);
        platform.set_title(title);
        platform.on_frame();
    }

    spdlog::info("voxov_cleanup — shutting down");
    runtime.shutdown();
    platform.shutdown();
    if (server_started) {
        server.shutdown();
    }
    return 0;
}
