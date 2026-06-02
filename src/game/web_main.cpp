/// Voxov web entry point.
///
/// sokol_app owns the browser canvas, WebGL context, event callbacks, and
/// Emscripten main loop. GameRuntime owns gameplay and rendering.

#include "game/game_runtime.hpp"
#include "game/web_runtime_adapter.hpp"
#include "platform/platform.hpp"
#include "platform/platform_services.hpp"

#include "sokol_app.h"
#include "sokol_log.h"

#include <emscripten/emscripten.h>

#include <algorithm>
#include <cstdio>
#include <string>

EM_JS(int, web_net_available, (), {
    return Module.__voxovNetApiReady ? 1 : 0;
});

EM_JS(void, web_configure_page, (), {
    if (Module.__voxovPageConfigured) {
        return;
    }
    Module.__voxovPageConfigured = true;
    Module.__voxovNetApiReady = !!Module.__voxovNetHost || !!Module.__voxovNetJoin;

    let viewport = document.querySelector('meta[name="viewport"]');
    if (!viewport) {
        viewport = document.createElement("meta");
        viewport.name = "viewport";
        document.head.appendChild(viewport);
    }
    viewport.content = "width=device-width, initial-scale=1, viewport-fit=cover";

    document.documentElement.style.width = "100%";
    document.documentElement.style.height = "100%";
    document.documentElement.style.background = "#090c14";
    document.body.style.margin = "0";
    document.body.style.width = "100%";
    document.body.style.height = "100%";
    document.body.style.overflow = "hidden";
    document.body.style.background = "#090c14";
    document.body.style.overscrollBehavior = "none";

    const canvas = Module.canvas || document.getElementById("canvas");
    if (canvas) {
        canvas.style.position = "fixed";
        canvas.style.inset = "0";
        canvas.style.width = "100vw";
        canvas.style.height = "100vh";
        canvas.style.display = "block";
        canvas.style.touchAction = "none";
        canvas.style.boxSizing = "border-box";
        canvas.style.background = "#090c14";
        canvas.tabIndex = 0;
        setTimeout(function() { canvas.focus(); }, 0);
    }

    const panel = document.createElement("pre");
    panel.id = "voxov-menu";
    panel.style.position = "fixed";
    panel.style.left = "calc(env(safe-area-inset-left, 0px) + 12px)";
    panel.style.top = "calc(env(safe-area-inset-top, 0px) + 12px)";
    panel.style.maxWidth = "calc(100vw - env(safe-area-inset-left, 0px) - env(safe-area-inset-right, 0px) - 24px)";
    panel.style.maxHeight = "calc(100vh - env(safe-area-inset-top, 0px) - env(safe-area-inset-bottom, 0px) - 24px)";
    panel.style.overflow = "auto";
    panel.style.padding = "12px 14px";
    panel.style.margin = "0";
    panel.style.whiteSpace = "pre";
    panel.style.fontFamily = "monospace";
    panel.style.fontSize = "clamp(12px, 1.5vw, 14px)";
    panel.style.lineHeight = "1.3";
    panel.style.color = "#e7edf7";
    panel.style.background = "rgba(8, 12, 20, 0.78)";
    panel.style.border = "1px solid rgba(150, 170, 210, 0.38)";
    panel.style.borderRadius = "8px";
    panel.style.boxSizing = "border-box";
    panel.style.zIndex = "9999";
    panel.style.pointerEvents = "none";
    panel.style.display = "none";
    document.body.appendChild(panel);
});

EM_JS(void, web_update_overlay, (const char *text), {
    const el = document.getElementById("voxov-menu");
    if (!el) {
        return;
    }
    const value = UTF8ToString(text);
    if (value && value.length > 0) {
        el.textContent = value;
        el.style.display = "block";
    } else {
        el.textContent = "";
        el.style.display = "none";
    }
});

namespace {

DesktopPlatform *g_platform = nullptr;
WebRuntimePlatformAdapter *g_runtime_platform = nullptr;
WebRuntimeInputAdapter *g_runtime_input = nullptr;
GameRuntime *g_runtime = nullptr;
PlatformServices *g_platform_services = nullptr;

std::string build_overlay_text(const RenderStats &stats) {
    char buffer[256]{};
    std::snprintf(
        buffer,
        sizeof(buffer),
        "VOXOV WEB\nFPS %.1f  CPU %.2fms  RENDER %.2fms\nFIXED %.2fms x%u  CHUNKS %u\nNET %s  REM %u  HOOKS %s",
        stats.fps,
        stats.cpu_ms,
        stats.render_cpu_ms,
        stats.fixed_cpu_ms,
        stats.fixed_steps,
        stats.streamed_chunk_count,
        stats.net_connected ? "CONNECTED" : "LOCAL",
        stats.net_remote_count,
        web_net_available() ? "READY" : "LOCAL");
    return std::string(buffer);
}

void voxov_init() {
    web_configure_page();

    g_platform = new DesktopPlatform();
    const PlatformCreateInfo create_info{
        .title = "VOXOV",
        .width = sapp_width(),
        .height = sapp_height(),
        .fullscreen = false,
        .backend = RenderBackendType::Sokol,
    };
    g_platform->init(create_info);

    g_runtime = new GameRuntime();
    g_runtime_platform = new WebRuntimePlatformAdapter(*g_platform);
    g_runtime_input = new WebRuntimeInputAdapter(*g_platform);
    g_platform_services = new PlatformServices(PlatformServices::web());

    GameRuntimeInitParams init_params{};
    init_params.platform = RuntimePlatform::Web;
    init_params.options.physics_backend = PhysicsSolverBackend::AvbdExperimental;
    init_params.options.render_backend = RenderBackendType::Sokol;
    init_params.input_adapter = g_runtime_input;
    init_params.platform_adapter = g_runtime_platform;
    init_params.platform_services = g_platform_services;

    if (!g_runtime->init(init_params)) {
        std::fprintf(stderr, "Web runtime init failed\n");
        sapp_request_quit();
    }
}

void voxov_frame() {
    if (!g_runtime || !g_platform) {
        return;
    }

    const double dt = std::clamp(sapp_frame_duration(), 1.0 / 240.0, 1.0 / 15.0);
    g_runtime->tick(dt);

    const RenderStats &stats = g_runtime->stats();
    web_update_overlay(build_overlay_text(stats).c_str());
    g_platform->on_frame();

    if (g_runtime->should_close()) {
        sapp_request_quit();
    }
}

void voxov_cleanup() {
    web_update_overlay("");

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
    delete g_platform_services;
    g_platform_services = nullptr;
}

void voxov_event(const sapp_event *event) {
    if (g_platform && event) {
        g_platform->process_event(*event);
    }
}

} // namespace

int main() {
    sapp_desc desc = {};
    desc.init_cb = voxov_init;
    desc.frame_cb = voxov_frame;
    desc.cleanup_cb = voxov_cleanup;
    desc.event_cb = voxov_event;
    desc.width = 1280;
    desc.height = 720;
    desc.window_title = "VOXOV";
    desc.html5.canvas_selector = "#canvas";
    desc.html5.canvas_resize = true;
    desc.html5.use_emsc_set_main_loop = true;
    desc.logger.func = slog_func;
    sapp_run(&desc);
    return 0;
}
