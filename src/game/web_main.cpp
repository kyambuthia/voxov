#include "engine_input/input_state.hpp"
#include "engine_runtime/runtime_session_controller.hpp"
#include "engine_ui/gui_menu.hpp"
#include "game/web_session_flow.hpp"

#include <GLES3/gl3.h>
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

EM_JS(int, web_poll_input_flags, (), {
    if (!Module.__voxovPollInputFlags) {
        return 0;
    }
    return Module.__voxovPollInputFlags() | 0;
});

EM_JS(int, web_net_available, (), {
    return Module.__voxovNetApiReady ? 1 : 0;
});

EM_JS(void, web_net_host, (), {
    if (Module.__voxovNetHost) {
        Module.__voxovNetHost();
    }
});

EM_JS(void, web_net_join, (), {
    if (Module.__voxovNetJoin) {
        Module.__voxovNetJoin();
    }
});

EM_JS(void, web_net_send_local, (float x, float y, float z, float yaw), {
    if (Module.__voxovNetSendLocal) {
        Module.__voxovNetSendLocal(x, y, z, yaw);
    }
});

EM_JS(int, web_net_remote_count, (), {
    if (Module.__voxovNetRemoteCount) {
        return Module.__voxovNetRemoteCount() | 0;
    }
    return 0;
});

namespace {
struct WebAppState {
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context = 0;
    float t = 0.0f;
    GuiMenu menu{};
    RuntimeSessionController session_controller{};
    WebSessionFlow session_flow{};
    int remote_count = 0;

    float x = 8.0f;
    float y = 6.0f;
    float z = 8.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    float vz = 0.0f;
    float yaw = 3.14159f;
    struct Telemetry {
        int frames = 0;
        double frame_ms_sum = 0.0;
        double fps = 0.0;
        double frame_ms = 0.0;
    } telemetry{};
};

InputState poll_web_input() {
    InputState out{};
    const int flags = web_poll_input_flags();
    out.menu_toggle_pressed = (flags & (1 << 0)) != 0;
    out.menu_up_pressed = (flags & (1 << 1)) != 0;
    out.menu_down_pressed = (flags & (1 << 2)) != 0;
    out.menu_select_pressed = (flags & (1 << 3)) != 0;
    out.move.y += (flags & (1 << 4)) ? 1.0f : 0.0f;  // W
    out.move.y -= (flags & (1 << 5)) ? 1.0f : 0.0f;  // S
    out.move.x -= (flags & (1 << 6)) ? 1.0f : 0.0f;  // A
    out.move.x += (flags & (1 << 7)) ? 1.0f : 0.0f;  // D
    out.sprint_held = (flags & (1 << 8)) != 0;
    out.jump_held = (flags & (1 << 9)) != 0;
    out.jump_pressed = out.jump_held;
    out.crouch_held = (flags & (1 << 10)) != 0;
    out.look_delta.x += (flags & (1 << 11)) ? -2.2f : 0.0f;
    out.look_delta.x += (flags & (1 << 12)) ? 2.2f : 0.0f;
    return out;
}

void update_web_overlay(const std::string &text) {
    EM_ASM(
        {
            const t = UTF8ToString($0);
            const el = document.getElementById("voxov-menu");
            if (!el) return;
            if (t && t.length > 0) {
                el.style.display = "block";
                el.textContent = t;
            } else {
                el.style.display = "none";
                el.textContent = "";
            }
        },
        text.c_str());
}

void simulate_local_player(WebAppState &state, const InputState &input, float dt) {
    if (state.menu.open() || !state.session_controller.gameplay_started()) {
        state.vx *= 0.9f;
        state.vz *= 0.9f;
        return;
    }

    const float speed = input.sprint_held ? 8.5f : 5.2f;
    const float c = std::cos(state.yaw);
    const float s = std::sin(state.yaw);
    const float dx = input.move.x * c - input.move.y * s;
    const float dz = input.move.x * s + input.move.y * c;
    state.vx = dx * speed;
    state.vz = dz * speed;
    state.yaw += input.look_delta.x * dt * 1.2f;

    if (input.jump_pressed && state.y <= 6.02f) {
        state.vy = 5.4f;
    }
    state.vy += -17.5f * dt;

    state.x += state.vx * dt;
    state.z += state.vz * dt;
    state.y += state.vy * dt;
    if (state.y < 6.0f) {
        state.y = 6.0f;
        state.vy = 0.0f;
    }
}

std::string build_overlay_text(const WebAppState &state) {
    const RuntimeSessionSnapshot snapshot = state.session_flow.snapshot();

    if (state.menu.open()) {
        return state.menu.build_text(
            state.session_controller.devhud_enabled(),
            state.session_controller.noclip_enabled(),
            state.session_controller.build_session_context(snapshot));
    }

    if (!state.session_controller.devhud_enabled()) {
        return {};
    }

    char buffer[256]{};
        std::snprintf(
        buffer,
        sizeof(buffer),
        "WEB DEVHUD\nP %.1f %.1f %.1f\nV %.1f %.1f %.1f\nYAW %.2f REM %d\nTEL FPS %.1f FT %.2f\nNET %s",
        state.x,
        state.y,
        state.z,
        state.vx,
        state.vy,
        state.vz,
        state.yaw,
        state.remote_count,
        state.telemetry.fps,
        state.telemetry.frame_ms,
        web_net_available() ? "HOOKED" : "LOCAL");
    return std::string(buffer);
}

void tick(void *arg) {
    auto *state = static_cast<WebAppState *>(arg);
    constexpr float dt = 1.0f / 60.0f;
    state->t += dt;

    const InputState input = poll_web_input();
    const RuntimeSessionMenuCallbacks callbacks{
        .leave_session = [state]() {
            state->session_flow.leave_session();
        },
        .host_local = [state]() {
            state->session_flow.host_local_session();
            web_net_host();
        },
        .host_lan = [state]() {
            state->session_flow.host_lan_session();
            web_net_host();
        },
        .join_nearby = [state]() {
            state->session_flow.join_nearby_session();
            web_net_join();
        }};
    (void)state->session_controller.handle_menu_input(input, state->menu, callbacks);

    simulate_local_player(*state, input, dt);
    web_net_send_local(state->x, state->y, state->z, state->yaw);
    state->remote_count = std::max(0, web_net_remote_count());
    state->session_flow.update(web_net_available());
    state->telemetry.frames += 1;
    state->telemetry.frame_ms_sum += static_cast<double>(dt) * 1000.0;
    if (state->telemetry.frames >= 30) {
        state->telemetry.frame_ms =
            state->telemetry.frame_ms_sum / static_cast<double>(state->telemetry.frames);
        state->telemetry.fps =
            1000.0 / std::max(0.001, state->telemetry.frame_ms);
        state->telemetry.frames = 0;
        state->telemetry.frame_ms_sum = 0.0;
    }

    const float move_energy = std::clamp(std::sqrt(state->vx * state->vx + state->vz * state->vz) / 9.0f, 0.0f, 1.0f);
    const float remote_tint = std::clamp(static_cast<float>(state->remote_count) / 6.0f, 0.0f, 1.0f);
    const float r = 0.10f + 0.18f * move_energy + 0.05f * std::sin(state->t * 1.1f);
    const float g = 0.14f + 0.12f * (1.0f - move_energy) + 0.05f * std::sin(state->t * 0.8f + 1.0f);
    const float b = 0.20f + 0.28f * remote_tint + 0.05f * std::sin(state->t * 1.3f + 2.0f);

    glViewport(0, 0, 1280, 720);
    if (state->menu.open()) {
        glClearColor(0.06f, 0.07f, 0.1f, 1.0f);
    } else {
        glClearColor(r, g, b, 1.0f);
    }
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    update_web_overlay(build_overlay_text(*state));
}
}

int main() {
    EmscriptenWebGLContextAttributes attrs{};
    emscripten_webgl_init_context_attributes(&attrs);
    attrs.alpha = EM_TRUE;
    attrs.depth = EM_TRUE;
    attrs.stencil = EM_FALSE;
    attrs.antialias = EM_TRUE;
    attrs.majorVersion = 2;
    attrs.minorVersion = 0;

    WebAppState state{};
    state.context = emscripten_webgl_create_context("#canvas", &attrs);
    if (state.context <= 0) {
        std::fprintf(stderr, "Failed to create WebGL2 context\n");
        return 1;
    }
    if (emscripten_webgl_make_context_current(state.context) != EMSCRIPTEN_RESULT_SUCCESS) {
        std::fprintf(stderr, "Failed to activate WebGL2 context\n");
        return 1;
    }

    EM_ASM(
        {
            if (!Module.__voxovGuiInit) {
                Module.__voxovGuiInit = true;
                Module.__voxovGuiFlags = 0;
                Module.__voxovGameFlags = 0;
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
                }

                Module.__voxovPollInputFlags = function() {
                    const out = (Module.__voxovGuiFlags | Module.__voxovGameFlags) | 0;
                    Module.__voxovGuiFlags = 0;
                    Module.__voxovGameFlags = 0;
                    return out;
                };

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
                panel.style.background = "rgba(8, 12, 20, 0.85)";
                panel.style.border = "1px solid rgba(150, 170, 210, 0.45)";
                panel.style.borderRadius = "10px";
                panel.style.boxSizing = "border-box";
                panel.style.zIndex = "9999";
                panel.style.display = "none";
                document.body.appendChild(panel);

                window.addEventListener("keydown", function(ev) {
                    if (ev.key === "Escape") Module.__voxovGuiFlags |= (1 << 0);
                    else if (ev.key === "ArrowUp") Module.__voxovGuiFlags |= (1 << 1);
                    else if (ev.key === "ArrowDown") Module.__voxovGuiFlags |= (1 << 2);
                    else if (ev.key === "Enter" || ev.key === " ") Module.__voxovGuiFlags |= (1 << 3);
                    else if (ev.key === "w" || ev.key === "W") Module.__voxovGameFlags |= (1 << 4);
                    else if (ev.key === "s" || ev.key === "S") Module.__voxovGameFlags |= (1 << 5);
                    else if (ev.key === "a" || ev.key === "A") Module.__voxovGameFlags |= (1 << 6);
                    else if (ev.key === "d" || ev.key === "D") Module.__voxovGameFlags |= (1 << 7);
                    else if (ev.key === "Shift") Module.__voxovGameFlags |= (1 << 8);
                    else if (ev.key === "Space") Module.__voxovGameFlags |= (1 << 9);
                    else if (ev.key === "Control") Module.__voxovGameFlags |= (1 << 10);
                    else if (ev.key === "ArrowLeft") Module.__voxovGameFlags |= (1 << 11);
                    else if (ev.key === "ArrowRight") Module.__voxovGameFlags |= (1 << 12);
                });
            }
        });

    glEnable(GL_DEPTH_TEST);
    emscripten_set_main_loop_arg(tick, &state, 0, 1);
    return 0;
}
