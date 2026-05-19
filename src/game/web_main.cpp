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
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

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
struct SceneVertex {
    float px = 0.0f;
    float py = 0.0f;
    float pz = 0.0f;
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
};

struct SceneMeshGpu {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ibo = 0;
    GLsizei index_count = 0;
};

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
    GLuint scene_program = 0;
    GLint u_mvp = -1;
    SceneMeshGpu ground_mesh{};
    SceneMeshGpu player_mesh{};
    bool scene_ready = false;
};

GLuint compile_shader(GLenum type, const char *source) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok == GL_TRUE) {
        return shader;
    }
    char log[512]{};
    glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
    std::fprintf(stderr, "Web shader compile failed: %s\n", log);
    glDeleteShader(shader);
    return 0;
}

GLuint create_scene_program() {
    static constexpr const char *k_vs = R"(
        #version 300 es
        precision highp float;
        layout(location = 0) in vec3 a_pos;
        layout(location = 1) in vec3 a_color;
        uniform mat4 u_mvp;
        out vec3 v_color;
        void main() {
            v_color = a_color;
            gl_Position = u_mvp * vec4(a_pos, 1.0);
        }
    )";
    static constexpr const char *k_fs = R"(
        #version 300 es
        precision highp float;
        in vec3 v_color;
        out vec4 o_color;
        void main() {
            o_color = vec4(v_color, 1.0);
        }
    )";

    const GLuint vs = compile_shader(GL_VERTEX_SHADER, k_vs);
    const GLuint fs = compile_shader(GL_FRAGMENT_SHADER, k_fs);
    if (vs == 0 || fs == 0) {
        if (vs != 0) glDeleteShader(vs);
        if (fs != 0) glDeleteShader(fs);
        return 0;
    }

    const GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (ok == GL_TRUE) {
        return program;
    }
    char log[512]{};
    glGetProgramInfoLog(program, sizeof(log), nullptr, log);
    std::fprintf(stderr, "Web shader link failed: %s\n", log);
    glDeleteProgram(program);
    return 0;
}

SceneMeshGpu upload_scene_mesh(
    const std::vector<SceneVertex> &vertices,
    const std::vector<uint16_t> &indices) {
    SceneMeshGpu mesh{};
    if (vertices.empty() || indices.empty()) {
        return mesh;
    }

    glGenVertexArrays(1, &mesh.vao);
    glBindVertexArray(mesh.vao);

    glGenBuffers(1, &mesh.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(vertices.size() * sizeof(SceneVertex)),
        vertices.data(),
        GL_STATIC_DRAW);

    glGenBuffers(1, &mesh.ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ibo);
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(indices.size() * sizeof(uint16_t)),
        indices.data(),
        GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(SceneVertex), reinterpret_cast<void *>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        1,
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(SceneVertex),
        reinterpret_cast<void *>(3 * sizeof(float)));

    glBindVertexArray(0);
    mesh.index_count = static_cast<GLsizei>(indices.size());
    return mesh;
}

void draw_scene_mesh(
    const SceneMeshGpu &mesh,
    GLuint program,
    GLint u_mvp,
    const glm::mat4 &mvp) {
    if (mesh.vao == 0 || mesh.index_count <= 0) {
        return;
    }
    glUseProgram(program);
    glUniformMatrix4fv(u_mvp, 1, GL_FALSE, glm::value_ptr(mvp));
    glBindVertexArray(mesh.vao);
    glDrawElements(GL_TRIANGLES, mesh.index_count, GL_UNSIGNED_SHORT, nullptr);
    glBindVertexArray(0);
}

bool init_scene_resources(WebAppState &state) {
    state.scene_program = create_scene_program();
    if (state.scene_program == 0) {
        return false;
    }
    state.u_mvp = glGetUniformLocation(state.scene_program, "u_mvp");
    if (state.u_mvp < 0) {
        std::fprintf(stderr, "Web scene uniform lookup failed\n");
        return false;
    }

    const std::vector<SceneVertex> ground_vertices{
        {-64.0f, 5.5f, -64.0f, 0.15f, 0.22f, 0.16f},
        {64.0f, 5.5f, -64.0f, 0.15f, 0.22f, 0.16f},
        {64.0f, 5.5f, 64.0f, 0.15f, 0.22f, 0.16f},
        {-64.0f, 5.5f, 64.0f, 0.15f, 0.22f, 0.16f},
    };
    const std::vector<uint16_t> ground_indices{0, 1, 2, 0, 2, 3};
    state.ground_mesh = upload_scene_mesh(ground_vertices, ground_indices);

    const std::vector<SceneVertex> player_vertices{
        {-0.3f, 0.0f, -0.3f, 0.82f, 0.86f, 0.95f},
        {0.3f, 0.0f, -0.3f, 0.82f, 0.86f, 0.95f},
        {0.3f, 0.0f, 0.3f, 0.82f, 0.86f, 0.95f},
        {-0.3f, 0.0f, 0.3f, 0.82f, 0.86f, 0.95f},
        {-0.3f, 1.2f, -0.3f, 0.95f, 0.78f, 0.56f},
        {0.3f, 1.2f, -0.3f, 0.95f, 0.78f, 0.56f},
        {0.3f, 1.2f, 0.3f, 0.95f, 0.78f, 0.56f},
        {-0.3f, 1.2f, 0.3f, 0.95f, 0.78f, 0.56f},
    };
    const std::vector<uint16_t> player_indices{
        0, 1, 2, 0, 2, 3, // bottom
        4, 5, 6, 4, 6, 7, // top
        0, 1, 5, 0, 5, 4, // side
        1, 2, 6, 1, 6, 5, // side
        2, 3, 7, 2, 7, 6, // side
        3, 0, 4, 3, 4, 7, // side
    };
    state.player_mesh = upload_scene_mesh(player_vertices, player_indices);

    state.scene_ready =
        state.ground_mesh.vao != 0 &&
        state.player_mesh.vao != 0;
    if (!state.scene_ready) {
        std::fprintf(stderr, "Web scene mesh upload failed\n");
    }
    return state.scene_ready;
}

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

    int viewport_w = 1280;
    int viewport_h = 720;
    (void)emscripten_get_canvas_element_size("#canvas", &viewport_w, &viewport_h);
    if (viewport_w <= 0 || viewport_h <= 0) {
        viewport_w = 1280;
        viewport_h = 720;
    }
    glViewport(0, 0, viewport_w, viewport_h);
    if (state->menu.open()) {
        glClearColor(0.06f, 0.07f, 0.1f, 1.0f);
    } else {
        glClearColor(r, g, b, 1.0f);
    }
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (state->scene_ready) {
        const glm::vec3 player_position(state->x, state->y, state->z);
        const glm::vec3 forward(-std::sin(state->yaw), 0.0f, -std::cos(state->yaw));
        const glm::vec3 camera_target = player_position + glm::vec3(0.0f, 1.0f, 0.0f);
        const glm::vec3 camera_position =
            camera_target - forward * 8.0f + glm::vec3(0.0f, 4.5f, 0.0f);
        const glm::mat4 view = glm::lookAt(
            camera_position,
            camera_target,
            glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 proj = glm::perspective(
            glm::radians(70.0f),
            static_cast<float>(viewport_w) / static_cast<float>(viewport_h),
            0.1f,
            256.0f);

        draw_scene_mesh(
            state->ground_mesh,
            state->scene_program,
            state->u_mvp,
            proj * view * glm::mat4(1.0f));
        draw_scene_mesh(
            state->player_mesh,
            state->scene_program,
            state->u_mvp,
            proj * view * glm::translate(glm::mat4(1.0f), player_position));

    }

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
    if (!init_scene_resources(state)) {
        return 1;
    }

    EM_ASM(
        {
            if (!Module.__voxovGuiInit) {
                Module.__voxovGuiInit = true;
                Module.__voxovGuiPulseFlags = 0;
                Module.__voxovGamePulseFlags = 0;
                Module.__voxovGameHeldFlags = 0;
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
                    const out = (
                        Module.__voxovGuiPulseFlags |
                        Module.__voxovGamePulseFlags |
                        Module.__voxovGameHeldFlags
                    ) | 0;
                    Module.__voxovGuiPulseFlags = 0;
                    Module.__voxovGamePulseFlags = 0;
                    return out;
                };

                const GAME_W = (1 << 4);
                const GAME_S = (1 << 5);
                const GAME_A = (1 << 6);
                const GAME_D = (1 << 7);
                const GAME_SHIFT = (1 << 8);
                const GAME_SPACE = (1 << 9);
                const GAME_CTRL = (1 << 10);
                const GAME_LEFT = (1 << 11);
                const GAME_RIGHT = (1 << 12);
                const heldBitForKey = function(key) {
                    if (key === "w" || key === "W") return GAME_W;
                    if (key === "s" || key === "S") return GAME_S;
                    if (key === "a" || key === "A") return GAME_A;
                    if (key === "d" || key === "D") return GAME_D;
                    if (key === "Shift") return GAME_SHIFT;
                    if (key === " ") return GAME_SPACE;
                    if (key === "Control") return GAME_CTRL;
                    if (key === "ArrowLeft") return GAME_LEFT;
                    if (key === "ArrowRight") return GAME_RIGHT;
                    return 0;
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
                    const heldBit = heldBitForKey(ev.key);
                    if (heldBit !== 0) {
                        Module.__voxovGameHeldFlags |= heldBit;
                        ev.preventDefault();
                        return;
                    }

                    if (ev.repeat) {
                        return;
                    }

                    if (ev.key === "Escape") {
                        Module.__voxovGuiPulseFlags |= (1 << 0);
                        ev.preventDefault();
                    } else if (ev.key === "ArrowUp") {
                        Module.__voxovGuiPulseFlags |= (1 << 1);
                        ev.preventDefault();
                    } else if (ev.key === "ArrowDown") {
                        Module.__voxovGuiPulseFlags |= (1 << 2);
                        ev.preventDefault();
                    } else if (ev.key === "Enter") {
                        Module.__voxovGuiPulseFlags |= (1 << 3);
                        ev.preventDefault();
                    }
                });

                window.addEventListener("keyup", function(ev) {
                    const heldBit = heldBitForKey(ev.key);
                    if (heldBit !== 0) {
                        Module.__voxovGameHeldFlags &= ~heldBit;
                        ev.preventDefault();
                    }
                });

                window.addEventListener("blur", function() {
                    Module.__voxovGameHeldFlags = 0;
                });
            }
        });

    glEnable(GL_DEPTH_TEST);
    emscripten_set_main_loop_arg(tick, &state, 0, 1);
    return 0;
}
