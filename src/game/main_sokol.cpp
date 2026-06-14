/// Voxov sokol_app entry point.
///
/// sokol_app owns the main loop via callbacks; all game logic runs
/// inside the frame callback.

#include "engine_core/timing.hpp"
#include "engine_net/net_server.hpp"
#include "game/desktop_runtime_adapter.hpp"
#include "game/game_runtime.hpp"
#include "platform/platform.hpp"
#include "platform/platform_services.hpp"

#include "sokol_app.h"
#include "sokol_log.h"

#include <spdlog/spdlog.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <thread>

// ---------------------------------------------------------------------------
// File-static globals (sokol callbacks are C function pointers — no captures).
// ---------------------------------------------------------------------------

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

AppOptions g_opts{};
std::atomic<bool> g_keep_running{true};

// Heap-allocated so we can control lifetime across callbacks.
DesktopPlatform *g_platform = nullptr;
DesktopRuntimePlatformAdapter *g_runtime_platform = nullptr;
DesktopRuntimeInputAdapter *g_runtime_input = nullptr;
GameRuntime *g_runtime = nullptr;
NetServer *g_server = nullptr;
FramePacer *g_pacer = nullptr;
bool g_server_started = false;

// ---------------------------------------------------------------------------
// sokol callbacks
// ---------------------------------------------------------------------------

void voxov_init() {
    spdlog::info("voxov_init — sokol_app window ready");

    // Platform
    g_platform = new DesktopPlatform();
    const PlatformCreateInfo create_info{
        .title = "VOXOV",
        .width = g_opts.window_width,
        .height = g_opts.window_height,
        .fullscreen = g_opts.start_fullscreen,
        .backend = RenderBackendType::Sokol,
    };
    g_platform->init(create_info);

    // Runtime
    g_runtime = new GameRuntime();
    g_runtime_platform = new DesktopRuntimePlatformAdapter(*g_platform);
    g_runtime_input = new DesktopRuntimeInputAdapter(*g_platform, false);
    const PlatformServices platform_services = PlatformServices::desktop_default();

    GameRuntimeInitParams init_params{};
    init_params.platform = RuntimePlatform::Desktop;
    GameRuntimeOptions options{};
    options.physics_backend = g_opts.physics_backend;
    options.render_backend = RenderBackendType::Sokol;
    init_params.options = options;
    init_params.input_adapter = g_runtime_input;
    init_params.platform_adapter = g_runtime_platform;
    init_params.platform_services = &platform_services;

    if (!g_runtime->init(init_params)) {
        spdlog::error("Runtime init failed");
        sapp_request_quit();
        return;
    }

    if (g_opts.connect_host) {
        g_runtime->connect(g_opts.connect_host, g_opts.connect_port);
    }

    g_pacer = new FramePacer();
    g_pacer->init(120.0);

    // Start server if needed.
    if (g_opts.run_server) {
        g_server = new NetServer();
        g_server_started = g_server->init(g_opts.connect_port);
    }
}

void voxov_frame() {
    if (!g_runtime || !g_pacer) return;

    g_pacer->begin_frame();

    if (g_server_started && g_server) {
        g_server->pump();
    }

    g_runtime->tick(g_pacer->frame_dt());

    // ── Debug screenshot system ──────────────────────────────────────
    // Auto-screenshot at key moments for visual debugging with Mimo Omni.
    // Also supports F5 for quick debug screenshot and F12 for timestamped.
    static int frame_counter = 0;
    frame_counter++;

    auto take_screenshot = [](const char *path) {
        std::system("mkdir -p screenshots 2>/dev/null");
        const int w = sapp_width();
        const int h = sapp_height();
        if (g_runtime->capture_screenshot(path, w, h)) {
            spdlog::info("Screenshot saved: {}", path);
        }
    };

    // Auto-screenshot after 120 frames (~2s) — terrain should be loaded.
    if (frame_counter == 120) {
        take_screenshot("screenshots/voxov_debug.png");
    }

    // F5: quick debug screenshot (overwrites same file for easy Mimo analysis).
    static bool f5_was_down = false;
    if (g_platform) {
        const bool f5_down = g_platform->input().keys_down.test(
            static_cast<size_t>(PlatformKey::F5));
        if (f5_down && !f5_was_down) {
            take_screenshot("screenshots/voxov_debug.png");
        }
        f5_was_down = f5_down;
    }

    // F12 screenshot capture (timestamped, keeps history).
    static bool f12_was_down = false;
    if (g_platform) {
        const bool f12_down = g_platform->input().keys_down.test(
            static_cast<size_t>(PlatformKey::F12));
        if (f12_down && !f12_was_down) {
            // Ensure screenshots directory exists.
            std::system("mkdir -p screenshots 2>/dev/null");
            char filepath[256]{};
            const auto now = std::chrono::system_clock::now();
            const auto t = std::chrono::system_clock::to_time_t(now);
            std::tm tm{};
            localtime_r(&t, &tm);
            std::snprintf(filepath, sizeof(filepath),
                          "screenshots/voxov_%04d%02d%02d_%02d%02d%02d.png",
                          tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                          tm.tm_hour, tm.tm_min, tm.tm_sec);
            const int w = sapp_width();
            const int h = sapp_height();
            if (g_runtime->capture_screenshot(filepath, w, h)) {
                spdlog::info("Screenshot saved: {}", filepath);
            } else {
                spdlog::warn("Screenshot failed: {}", filepath);
            }
        }
        f12_was_down = f12_down;
    }

    // F11 fullscreen toggle
    static bool f11_was_down = false;
    if (g_platform) {
        const bool f11_down = g_platform->input().keys_down.test(
            static_cast<size_t>(PlatformKey::F11));
        if (f11_down && !f11_was_down) {
            g_platform->toggle_fullscreen();
        }
        f11_was_down = f11_down;
    }

    g_pacer->end_frame();

    if (g_platform && g_runtime) {
        const RenderStats &stats = g_runtime->stats();
        char title[128]{};
        std::snprintf(title, sizeof(title),
                      "VOXOV  FPS: %.1f  CPU: %.2fms  NET: %u/%u Bps",
                      stats.fps, stats.cpu_ms, stats.net_tx_bytes_per_sec,
                      stats.net_rx_bytes_per_sec);
        g_platform->set_title(title);

        // Reset per-frame mouse deltas
        g_platform->on_frame();
    }

    if (!g_keep_running.load()) {
        sapp_request_quit();
    }
}

void voxov_cleanup() {
    spdlog::info("voxov_cleanup — shutting down");

    if (g_runtime) {
        g_runtime->shutdown();
        delete g_runtime;
        g_runtime = nullptr;
    }
    delete g_runtime_input;
    g_runtime_input = nullptr;
    delete g_runtime_platform;
    g_runtime_platform = nullptr;
    if (g_platform) {
        g_platform->shutdown();
        delete g_platform;
        g_platform = nullptr;
    }
    if (g_server) {
        if (g_server_started) g_server->shutdown();
        delete g_server;
        g_server = nullptr;
    }
    delete g_pacer;
    g_pacer = nullptr;
}

void voxov_event(const sapp_event *event) {
    if (g_platform) {
        g_platform->process_event(*event);
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(int argc, char **argv) {
    // Parse CLI
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--server") == 0) {
            g_opts.run_server = true;
        } else if (std::strcmp(argv[i], "--headless-server") == 0) {
            // Headless: no sokol_app window. Run NetServer and spin.
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
            g_opts.connect_host = argv[++i];
        } else if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            g_opts.connect_port = static_cast<uint16_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (std::strcmp(argv[i], "--physics") == 0 && i + 1 < argc) {
            if (std::strcmp(argv[++i], "avbd") == 0 || std::strcmp(argv[i], "avbd-experimental") == 0) {
                g_opts.physics_backend = PhysicsSolverBackend::AvbdExperimental;
            }
        } else if (std::strcmp(argv[i], "--fullscreen") == 0) {
            g_opts.start_fullscreen = true;
        } else if (std::strcmp(argv[i], "--windowed") == 0) {
            g_opts.start_fullscreen = false;
        } else if (std::strcmp(argv[i], "--width") == 0 && i + 1 < argc) {
            g_opts.window_width = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--height") == 0 && i + 1 < argc) {
            g_opts.window_height = std::atoi(argv[++i]);
        }
    }

    // Build sokol_app descriptor
    sapp_desc desc = {};
    desc.init_cb = voxov_init;
    desc.frame_cb = voxov_frame;
    desc.cleanup_cb = voxov_cleanup;
    desc.event_cb = voxov_event;
    desc.width = g_opts.window_width;
    desc.height = g_opts.window_height;
    desc.fullscreen = g_opts.start_fullscreen;
    desc.window_title = "VOXOV";
#if defined(SOKOL_GLCORE) && defined(__linux__)
    // Sokol defaults Linux GLCORE to 4.3; older but still capable GPUs such as
    // Intel HD 3000 top out at GL 3.3 core.
    desc.gl.major_version = 3;
    desc.gl.minor_version = 3;
#endif
    desc.logger.func = slog_func;

    // Signal handlers
    std::signal(SIGINT, [](int) { g_keep_running = false; });
    std::signal(SIGTERM, [](int) { g_keep_running = false; });

    sapp_run(&desc);

    return 0;
}
