#include "engine_input/input_state.hpp"
#include "engine_ui/gui_menu.hpp"

#include <GLES3/gl3.h>
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

#include <cmath>
#include <cstdio>
#include <string>

namespace {
struct WebAppState {
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context = 0;
    float t = 0.0f;
    GuiMenu menu{};
    bool devhud = false;
    bool noclip = false;
};

InputState poll_web_menu_input() {
    InputState out{};
    const int flags = emscripten_run_script_int("Module.__voxovPollGuiFlags ? Module.__voxovPollGuiFlags() : 0");
    out.menu_toggle_pressed = (flags & 1) != 0;
    out.menu_up_pressed = (flags & 2) != 0;
    out.menu_down_pressed = (flags & 4) != 0;
    out.menu_select_pressed = (flags & 8) != 0;
    return out;
}

void update_web_menu_overlay(const std::string &text) {
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

void main_loop(void *arg) {
    auto *state = static_cast<WebAppState *>(arg);
    state->t += 1.0f / 60.0f;

    GuiMenuActions actions{};
    state->menu.handle_input(poll_web_menu_input(), state->devhud, state->noclip, actions);
    if (actions.start_game) {
        std::fprintf(stderr, "GUI start game requested\n");
    }
    if (actions.toggle_devhud) {
        state->devhud = !state->devhud;
    }
    if (actions.toggle_noclip) {
        state->noclip = !state->noclip;
    }
    if (actions.host_local) {
        std::fprintf(stderr, "GUI host local requested (network path not yet wired on web target)\n");
    }
    if (actions.join_local) {
        std::fprintf(stderr, "GUI join localhost requested (network path not yet wired on web target)\n");
    }

    update_web_menu_overlay(state->menu.build_text(state->devhud, state->noclip));

    const float r = 0.12f + 0.08f * std::sin(state->t * 0.9f);
    const float g = 0.18f + 0.08f * std::sin(state->t * 1.4f + 1.0f);
    const float b = 0.24f + 0.08f * std::sin(state->t * 1.1f + 2.0f);

    glViewport(0, 0, 1280, 720);
    if (state->menu.open()) {
        glClearColor(0.06f, 0.07f, 0.1f, 1.0f);
    } else {
        glClearColor(r, g, b, 1.0f);
    }
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
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
                Module.__voxovPollGuiFlags = function() {
                    const out = Module.__voxovGuiFlags | 0;
                    Module.__voxovGuiFlags = 0;
                    return out;
                };

                const panel = document.createElement("pre");
                panel.id = "voxov-menu";
                panel.style.position = "fixed";
                panel.style.left = "12px";
                panel.style.top = "12px";
                panel.style.padding = "10px 12px";
                panel.style.margin = "0";
                panel.style.whiteSpace = "pre";
                panel.style.fontFamily = "monospace";
                panel.style.fontSize = "13px";
                panel.style.lineHeight = "1.3";
                panel.style.color = "#e7edf7";
                panel.style.background = "rgba(8, 12, 20, 0.85)";
                panel.style.border = "1px solid rgba(150, 170, 210, 0.45)";
                panel.style.borderRadius = "8px";
                panel.style.zIndex = "9999";
                panel.style.display = "none";
                document.body.appendChild(panel);

                window.addEventListener("keydown", function(ev) {
                    if (ev.key === "Escape") Module.__voxovGuiFlags |= 1;
                    else if (ev.key === "ArrowUp" || ev.key === "w" || ev.key === "W") Module.__voxovGuiFlags |= 2;
                    else if (ev.key === "ArrowDown" || ev.key === "s" || ev.key === "S") Module.__voxovGuiFlags |= 4;
                    else if (ev.key === "Enter" || ev.key === " ") Module.__voxovGuiFlags |= 8;
                });
            }
        });

    glEnable(GL_DEPTH_TEST);
    emscripten_set_main_loop_arg(main_loop, &state, 0, 1);
    return 0;
}
