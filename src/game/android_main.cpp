#include "engine_world/voxel_chunk.hpp"
#include "engine_world/physics/voxel_collision.hpp"
#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include "engine_render/debug_draw/debug_draw.hpp"
#include "engine_ui/gui_menu.hpp"
#include "engine_input/input_state.hpp"
#include "engine_audio/ui_audio.hpp"
#include "engine_net/net_client.hpp"
#include "engine_net/lan_discovery.hpp"
#include "engine_net/net_server.hpp"

#include <android/input.h>
#include <android/log.h>
#include <android_native_app_glue.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace {
constexpr const char *kLogTag = "VOXOV";
constexpr uint16_t kLocalPlayPort = 7777;
constexpr const char *kLocalPlayHost = "127.0.0.1";
constexpr double kConnectTimeoutSeconds = 5.0;

#ifndef EGL_OPENGL_ES3_BIT
#ifdef EGL_OPENGL_ES3_BIT_KHR
#define EGL_OPENGL_ES3_BIT EGL_OPENGL_ES3_BIT_KHR
#else
#define EGL_OPENGL_ES3_BIT 0x0040
#endif
#endif

const char *egl_error_to_string(EGLint err) {
    switch (err) {
    case EGL_SUCCESS: return "EGL_SUCCESS";
    case EGL_NOT_INITIALIZED: return "EGL_NOT_INITIALIZED";
    case EGL_BAD_ACCESS: return "EGL_BAD_ACCESS";
    case EGL_BAD_ALLOC: return "EGL_BAD_ALLOC";
    case EGL_BAD_ATTRIBUTE: return "EGL_BAD_ATTRIBUTE";
    case EGL_BAD_CONFIG: return "EGL_BAD_CONFIG";
    case EGL_BAD_CONTEXT: return "EGL_BAD_CONTEXT";
    case EGL_BAD_CURRENT_SURFACE: return "EGL_BAD_CURRENT_SURFACE";
    case EGL_BAD_DISPLAY: return "EGL_BAD_DISPLAY";
    case EGL_BAD_MATCH: return "EGL_BAD_MATCH";
    case EGL_BAD_NATIVE_PIXMAP: return "EGL_BAD_NATIVE_PIXMAP";
    case EGL_BAD_NATIVE_WINDOW: return "EGL_BAD_NATIVE_WINDOW";
    case EGL_BAD_PARAMETER: return "EGL_BAD_PARAMETER";
    case EGL_BAD_SURFACE: return "EGL_BAD_SURFACE";
    case EGL_CONTEXT_LOST: return "EGL_CONTEXT_LOST";
    default: return "EGL_UNKNOWN";
    }
}

GLuint compile_shader(GLenum type, const char *src) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok == GL_FALSE) {
        GLint len = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(std::max(1, len), 0);
        glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "shader compile failed: %s", log.data());
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint create_program() {
    static const char *kVs = R"(
        attribute vec3 aPos;
        attribute vec3 aColor;
        uniform mat4 uMVP;
        varying vec3 vColor;
        void main() {
            vColor = aColor;
            gl_Position = uMVP * vec4(aPos, 1.0);
        }
    )";
    static const char *kFs = R"(
        precision mediump float;
        varying vec3 vColor;
        void main() {
            gl_FragColor = vec4(vColor, 1.0);
        }
    )";

    const GLuint vs = compile_shader(GL_VERTEX_SHADER, kVs);
    const GLuint fs = compile_shader(GL_FRAGMENT_SHADER, kFs);
    if (!vs || !fs) {
        if (vs) {
            glDeleteShader(vs);
        }
        if (fs) {
            glDeleteShader(fs);
        }
        return 0;
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glBindAttribLocation(program, 0, "aPos");
    glBindAttribLocation(program, 1, "aColor");
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (ok == GL_FALSE) {
        GLint len = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(std::max(1, len), 0);
        glGetProgramInfoLog(program, static_cast<GLsizei>(log.size()), nullptr, log.data());
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "program link failed: %s", log.data());
        glDeleteProgram(program);
        return 0;
    }

    return program;
}

using GlyphRows = std::array<uint8_t, 7>;

const std::unordered_map<char, GlyphRows> kGlyphs = {
    {'A', {0x04, 0x0A, 0x11, 0x11, 0x1F, 0x11, 0x11}},
    {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
    {'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}},
    {'D', {0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C}},
    {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}},
    {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
    {'G', {0x0E, 0x11, 0x10, 0x10, 0x13, 0x11, 0x0E}},
    {'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'I', {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'J', {0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0E}},
    {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}},
    {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
    {'M', {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}},
    {'N', {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11}},
    {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
    {'Q', {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}},
    {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
    {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}},
    {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},
    {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11}},
    {'X', {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}},
    {'Y', {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}},
    {'Z', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}},
    {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}},
    {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},
    {'3', {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}},
    {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}},
    {'5', {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}},
    {'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}},
    {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},
    {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}},
    {'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}},
    {'>', {0x10, 0x08, 0x04, 0x02, 0x04, 0x08, 0x10}},
    {'/', {0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10}},
    {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x06}},
    {':', {0x00, 0x06, 0x06, 0x00, 0x06, 0x06, 0x00}},
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}}
};

GlyphRows glyph_for(char c) {
    const char uc = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    auto it = kGlyphs.find(uc);
    if (it != kGlyphs.end()) {
        return it->second;
    }
    return kGlyphs.at(' ');
}

void add_overlay_quad(
    RenderMesh &mesh,
    int width,
    int height,
    float x_px,
    float y_px,
    float size_px,
    const glm::vec3 &color) {
    const float x0 = (2.0f * x_px / static_cast<float>(width)) - 1.0f;
    const float y0 = 1.0f - (2.0f * y_px / static_cast<float>(height));
    const float x1 = (2.0f * (x_px + size_px) / static_cast<float>(width)) - 1.0f;
    const float y1 = 1.0f - (2.0f * (y_px + size_px) / static_cast<float>(height));

    const glm::vec3 p0(x0, y0, 0.0f);
    const glm::vec3 p1(x1, y0, 0.0f);
    const glm::vec3 p2(x1, y1, 0.0f);
    const glm::vec3 p3(x0, y1, 0.0f);

    const uint32_t start = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back({p0, color});
    mesh.vertices.push_back({p1, color});
    mesh.vertices.push_back({p2, color});
    mesh.vertices.push_back({p3, color});
    mesh.indices.insert(mesh.indices.end(), {start, start + 1, start + 2, start, start + 2, start + 3});
}

RenderMesh build_overlay_text_mesh(
    const std::string &text,
    int width,
    int height,
    float origin_x_px,
    float origin_y_px,
    float cell_size_px,
    const glm::vec3 &color,
    float line_spacing_scale = 1.0f) {
    RenderMesh mesh{};
    if (width <= 0 || height <= 0 || text.empty()) {
        return mesh;
    }

    float pen_x = origin_x_px;
    float pen_y = origin_y_px;
    const float char_advance = 6.0f * cell_size_px;
    const float line_advance = 8.0f * cell_size_px * std::max(1.0f, line_spacing_scale);

    for (char c : text) {
        if (c == '\n') {
            pen_x = origin_x_px;
            pen_y += line_advance;
            continue;
        }

        const GlyphRows glyph = glyph_for(c);
        for (int row = 0; row < 7; ++row) {
            for (int col = 0; col < 5; ++col) {
                if ((glyph[row] & (1 << (4 - col))) == 0) {
                    continue;
                }
                add_overlay_quad(
                    mesh,
                    width,
                    height,
                    pen_x + static_cast<float>(col) * cell_size_px,
                    pen_y + static_cast<float>(row) * cell_size_px,
                    cell_size_px,
                    color);
            }
        }
        pen_x += char_advance;
    }

    return mesh;
}

struct TouchState {
    int32_t left_pointer = -1;
    int32_t right_pointer = -1;
    glm::vec2 left_origin = glm::vec2(0.0f);
    glm::vec2 left_value = glm::vec2(0.0f);
    glm::vec2 right_prev = glm::vec2(0.0f);
    glm::vec2 look_delta = glm::vec2(0.0f);
};

struct GpuMesh {
    GLuint vbo = 0;
    GLuint ibo = 0;
    GLsizei index_count = 0;
    GLenum index_type = GL_UNSIGNED_INT;
};

struct AndroidRenderer {
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
    int32_t width = 0;
    int32_t height = 0;
    bool focused = false;
    int gles_version = 0;
    bool can_draw_uint_indices = false;

    GLuint program = 0;
    GLint u_mvp = -1;
    GpuMesh terrain_gpu{};
    GpuMesh grid_gpu{};
    GpuMesh capsule_gpu{};
    GpuMesh ui_text_gpu{};

    VoxelChunk world{};
    RenderMesh terrain_mesh{};
    RenderMesh grid_mesh{};
    RenderMesh capsule_mesh{};
    RenderMesh ui_text_mesh{};
    VoxelCollisionWorld collision_world{nullptr};
    glm::vec3 player_feet_position = glm::vec3(8.5f, 6.0f, 8.5f);
    float player_vertical_velocity = 0.0f;
    bool player_grounded = false;
    float player_capsule_radius = 0.45f;
    float player_capsule_height = 1.8f;

    glm::vec3 cam_pos = glm::vec3(8.0f, 8.0f, 22.0f);
    float camera_distance = 5.0f;
    float camera_pivot_height = 1.0f;
    float cam_yaw = 3.14159f;
    float cam_pitch = -0.25f;
    TouchState touch{};
    GuiMenu gui_menu{};
    UiAudio ui_audio{};
    bool audio_ready = false;
    bool menu_open_prev = false;
    bool devhud = false;
    bool noclip = false;
    bool gameplay_started = false;
    bool pending_menu_toggle = false;
    bool pending_menu_up = false;
    bool pending_menu_down = false;
    bool pending_menu_select = false;
    NetClient net_client{};
    LanDiscovery lan_discovery{};
    NetServer local_server{};
    bool net_initialized = false;
    bool net_connected = false;
    bool net_connecting = false;
    double net_connect_elapsed = 0.0;
    bool local_server_running = false;
    bool local_server_loopback = true;
    bool hosting_local = false;
    bool searching_nearby = false;
    uint32_t net_tick = 0;
    std::string ui_text_cache;
    std::string multiplayer_hint;

    timespec last_time{};
    bool has_last_time = false;
    uint64_t frame_counter = 0;

    void init_audio_if_needed() {
        if (audio_ready) {
            return;
        }
        audio_ready = ui_audio.init();
    }

    void init_network_if_needed() {
        if (net_initialized) {
            return;
        }
        net_client.init();
        net_initialized = true;
        net_connected = false;
        net_connecting = false;
        net_connect_elapsed = 0.0;
        net_tick = 0;
    }

    void stop_local_server() {
        if (!local_server_running) {
            return;
        }
        local_server.shutdown();
        local_server_running = false;
        local_server_loopback = true;
        hosting_local = false;
        __android_log_print(ANDROID_LOG_INFO, kLogTag, "Local server stopped");
    }

    void stop_client() {
        if (!net_initialized) {
            return;
        }
        net_client.disconnect();
        net_connected = false;
        net_connecting = false;
        net_connect_elapsed = 0.0;
    }

    void shutdown_network() {
        stop_client();
        stop_local_server();
        lan_discovery.stop();
        searching_nearby = false;
        if (net_initialized) {
            net_client.shutdown();
            net_initialized = false;
        }
    }

    void connect_local() {
        init_network_if_needed();
        if (net_connected || net_connecting) {
            return;
        }

        net_client.connect(kLocalPlayHost, kLocalPlayPort);
        NetChunkInterest interest{};
        interest.center_x = 0;
        interest.center_z = 0;
        interest.radius = 2;
        net_client.set_chunk_interest(interest);
        net_connecting = true;
        net_connect_elapsed = 0.0;
        __android_log_print(ANDROID_LOG_INFO, kLogTag, "Connecting to local server %s:%u", kLocalPlayHost, kLocalPlayPort);
    }

    void host_local_secure() {
        init_network_if_needed();
        if (!local_server_running) {
            local_server.init(kLocalPlayPort, true);
            local_server_running = true;
            local_server_loopback = true;
            hosting_local = true;
            __android_log_print(ANDROID_LOG_INFO, kLogTag, "Loopback-only local server started on %u", kLocalPlayPort);
        }
        lan_discovery.stop();
        searching_nearby = false;
        multiplayer_hint = "Hosting this device only.";
        connect_local();
    }

    void host_lan_secure() {
        init_network_if_needed();
        if (!local_server_running || local_server_loopback) {
            stop_local_server();
            local_server.init(kLocalPlayPort, false);
            local_server_running = true;
            local_server_loopback = false;
        }
        lan_discovery.start_host(kLocalPlayPort, "VOXOV Host");
        searching_nearby = false;
        multiplayer_hint = "Hosting Wi-Fi game. Friends tap Join Nearby.";
        connect_local();
    }

    void join_nearby_secure() {
        init_network_if_needed();
        lan_discovery.start_client();
        searching_nearby = true;
        multiplayer_hint = "Searching nearby Wi-Fi hosts...";
    }

    void pump_network(double dt_seconds) {
        if (local_server_running) {
            local_server.pump();
            if (!local_server_loopback) {
                lan_discovery.pump();
            }
        }
        if (!net_initialized) {
            return;
        }

        net_client.pump();
        const bool now_connected = net_client.is_connected();
        if (now_connected != net_connected) {
            net_connected = now_connected;
            net_connecting = !now_connected;
            net_connect_elapsed = 0.0;
            __android_log_print(ANDROID_LOG_INFO, kLogTag, "NetClient state changed: connected=%d", net_connected ? 1 : 0);
        }

        if (!net_connected && net_connecting) {
            net_connect_elapsed += dt_seconds;
            if (net_connect_elapsed >= kConnectTimeoutSeconds) {
                __android_log_print(ANDROID_LOG_WARN, kLogTag, "NetClient connect timeout after %.2fs", net_connect_elapsed);
                stop_client();
            }
        }

        if (!net_connected) {
            if (searching_nearby) {
                lan_discovery.pump();
                LanHostEntry host{};
                if (lan_discovery.pop_host(host)) {
                    net_client.connect(host.ip.c_str(), host.port);
                    NetChunkInterest interest{};
                    interest.center_x = 0;
                    interest.center_z = 0;
                    interest.radius = 2;
                    net_client.set_chunk_interest(interest);
                    net_connecting = true;
                    net_connect_elapsed = 0.0;
                    searching_nearby = false;
                    multiplayer_hint = "Joining " + host.name + " (" + host.ip + ")";
                }
            }
            return;
        }

        NetTickInput input{};
        input.tick = net_tick++;
        input.move_x = touch.left_value.x;
        input.move_y = touch.left_value.y;
        net_client.send_input(input);

        NetSnapshot snapshot{};
        if (net_client.poll_snapshot(snapshot)) {
            if (std::isfinite(snapshot.x) && std::isfinite(snapshot.z) &&
                std::fabs(snapshot.x) < 100000.0f && std::fabs(snapshot.z) < 100000.0f) {
                player_feet_position.x = snapshot.x;
                player_feet_position.z = snapshot.z;
            }
        }
    }

    bool can_render() const {
        return display != EGL_NO_DISPLAY && surface != EGL_NO_SURFACE && context != EGL_NO_CONTEXT;
    }

    void destroy_mesh(GpuMesh &mesh) {
        if (mesh.vbo != 0) {
            glDeleteBuffers(1, &mesh.vbo);
            mesh.vbo = 0;
        }
        if (mesh.ibo != 0) {
            glDeleteBuffers(1, &mesh.ibo);
            mesh.ibo = 0;
        }
        mesh.index_count = 0;
    }

    void shutdown_gl_resources() {
        destroy_mesh(terrain_gpu);
        destroy_mesh(grid_gpu);
        destroy_mesh(capsule_gpu);
        destroy_mesh(ui_text_gpu);
        if (program != 0) {
            glDeleteProgram(program);
            program = 0;
        }
        u_mvp = -1;
    }

    GpuMesh upload_mesh(const RenderMesh &mesh) {
        GpuMesh out{};
        if (mesh.vertices.empty() || mesh.indices.empty()) {
            return out;
        }

        glGenBuffers(1, &out.vbo);
        glBindBuffer(GL_ARRAY_BUFFER, out.vbo);
        glBufferData(
            GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(mesh.vertices.size() * sizeof(RenderVertex)),
            mesh.vertices.data(),
            GL_STATIC_DRAW);

        glGenBuffers(1, &out.ibo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, out.ibo);
        if (can_draw_uint_indices) {
            glBufferData(
                GL_ELEMENT_ARRAY_BUFFER,
                static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(uint32_t)),
                mesh.indices.data(),
                GL_STATIC_DRAW);
            out.index_type = GL_UNSIGNED_INT;
        } else {
            uint32_t max_index = 0;
            for (uint32_t idx : mesh.indices) {
                max_index = std::max(max_index, idx);
            }
            if (max_index > 65535u) {
                __android_log_print(
                    ANDROID_LOG_ERROR,
                    kLogTag,
                    "mesh index overflow for GLES2 path: max_index=%u, count=%u",
                    max_index,
                    static_cast<unsigned>(mesh.indices.size()));
                destroy_mesh(out);
                return out;
            }

            std::vector<uint16_t> indices16(mesh.indices.size());
            for (size_t i = 0; i < mesh.indices.size(); ++i) {
                indices16[i] = static_cast<uint16_t>(mesh.indices[i]);
            }
            glBufferData(
                GL_ELEMENT_ARRAY_BUFFER,
                static_cast<GLsizeiptr>(indices16.size() * sizeof(uint16_t)),
                indices16.data(),
                GL_STATIC_DRAW);
            out.index_type = GL_UNSIGNED_SHORT;
        }

        out.index_count = static_cast<GLsizei>(mesh.indices.size());
        return out;
    }

    bool initialize(android_app *app) {
        if (app == nullptr || app->window == nullptr || can_render()) {
            return false;
        }

        display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (display == EGL_NO_DISPLAY) {
            __android_log_print(ANDROID_LOG_ERROR, kLogTag, "eglGetDisplay failed: %s", egl_error_to_string(eglGetError()));
            return false;
        }

        EGLint major = 0;
        EGLint minor = 0;
        if (eglInitialize(display, &major, &minor) == EGL_FALSE) {
            __android_log_print(ANDROID_LOG_ERROR, kLogTag, "eglInitialize failed: %s", egl_error_to_string(eglGetError()));
            shutdown();
            return false;
        }

        const EGLint config_attrs_es3[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RED_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8,
            EGL_ALPHA_SIZE, 8,
            EGL_DEPTH_SIZE, 24,
            EGL_NONE
        };
        const EGLint config_attrs_es2[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RED_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8,
            EGL_ALPHA_SIZE, 8,
            EGL_DEPTH_SIZE, 24,
            EGL_NONE
        };

        EGLConfig config = nullptr;
        EGLint num_configs = 0;
        if ((eglChooseConfig(display, config_attrs_es3, &config, 1, &num_configs) == EGL_FALSE || num_configs < 1) &&
            (eglChooseConfig(display, config_attrs_es2, &config, 1, &num_configs) == EGL_FALSE || num_configs < 1)) {
            __android_log_print(ANDROID_LOG_ERROR, kLogTag, "eglChooseConfig failed: %s", egl_error_to_string(eglGetError()));
            shutdown();
            return false;
        }

        const EGLint context_attrs_es3[] = {
            EGL_CONTEXT_CLIENT_VERSION, 3,
            EGL_NONE
        };
        const EGLint context_attrs_es2[] = {
            EGL_CONTEXT_CLIENT_VERSION, 2,
            EGL_NONE
        };

        surface = eglCreateWindowSurface(display, config, app->window, nullptr);
        if (surface == EGL_NO_SURFACE) {
            __android_log_print(ANDROID_LOG_ERROR, kLogTag, "eglCreateWindowSurface failed: %s", egl_error_to_string(eglGetError()));
            shutdown();
            return false;
        }

        context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attrs_es3);
        gles_version = 3;
        if (context == EGL_NO_CONTEXT) {
            context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attrs_es2);
            gles_version = 2;
        }
        if (context == EGL_NO_CONTEXT) {
            __android_log_print(ANDROID_LOG_ERROR, kLogTag, "eglCreateContext failed: %s", egl_error_to_string(eglGetError()));
            shutdown();
            return false;
        }

        if (eglMakeCurrent(display, surface, surface, context) == EGL_FALSE) {
            __android_log_print(ANDROID_LOG_ERROR, kLogTag, "eglMakeCurrent failed: %s", egl_error_to_string(eglGetError()));
            shutdown();
            return false;
        }

        eglSwapInterval(display, 1);
        eglQuerySurface(display, surface, EGL_WIDTH, &width);
        eglQuerySurface(display, surface, EGL_HEIGHT, &height);

        const char *extensions = reinterpret_cast<const char *>(glGetString(GL_EXTENSIONS));
        const bool has_uint_ext = extensions != nullptr && std::string(extensions).find("GL_OES_element_index_uint") != std::string::npos;
        can_draw_uint_indices = (gles_version >= 3) || has_uint_ext;

        glEnable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDepthFunc(GL_LEQUAL);

        program = create_program();
        if (program == 0) {
            shutdown();
            return false;
        }
        init_audio_if_needed();
        u_mvp = glGetUniformLocation(program, "uMVP");

        world.generate_heightmap_terrain();
        collision_world = VoxelCollisionWorld(&world);
        player_feet_position.y = collision_world.find_spawn_height(
            glm::vec2(player_feet_position.x, player_feet_position.z),
            player_capsule_radius,
            player_capsule_height) +
            0.05f;
        player_vertical_velocity = 0.0f;
        player_grounded = false;
        terrain_mesh = world.build_naive_mesh();
        grid_mesh = world.build_debug_grid(64.0f, 1.0f);
        capsule_mesh = build_debug_capsule_mesh(
            glm::vec3(0.0f),
            player_capsule_radius,
            player_capsule_height,
            glm::vec3(0.95f, 0.5f, 0.2f));
        ui_text_mesh = RenderMesh{};
        ui_text_cache.clear();
        terrain_gpu = upload_mesh(terrain_mesh);
        grid_gpu = upload_mesh(grid_mesh);
        capsule_gpu = upload_mesh(capsule_mesh);

        clock_gettime(CLOCK_MONOTONIC, &last_time);
        has_last_time = true;
        frame_counter = 0;

        __android_log_print(
            ANDROID_LOG_INFO,
            kLogTag,
            "EGL ready (%d x %d), GLES%d: %s, uint_indices=%d terrain_tris=%d",
            width,
            height,
            gles_version,
            glGetString(GL_VERSION),
            can_draw_uint_indices ? 1 : 0,
            static_cast<int>(terrain_mesh.indices.size() / 3));
        return true;
    }

    void shutdown() {
        shutdown_network();
        if (audio_ready) {
            ui_audio.shutdown();
            audio_ready = false;
        }
        if (can_render()) {
            shutdown_gl_resources();
        }

        if (display != EGL_NO_DISPLAY) {
            eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        }
        if (context != EGL_NO_CONTEXT) {
            eglDestroyContext(display, context);
            context = EGL_NO_CONTEXT;
        }
        if (surface != EGL_NO_SURFACE) {
            eglDestroySurface(display, surface);
            surface = EGL_NO_SURFACE;
        }
        if (display != EGL_NO_DISPLAY) {
            eglTerminate(display);
            display = EGL_NO_DISPLAY;
        }

        width = 0;
        height = 0;
        gles_version = 0;
        can_draw_uint_indices = false;
        has_last_time = false;
        frame_counter = 0;
    }

    void process_gui_actions() {
        InputState menu_input{};
        menu_input.menu_toggle_pressed = pending_menu_toggle;
        menu_input.menu_up_pressed = pending_menu_up;
        menu_input.menu_down_pressed = pending_menu_down;
        menu_input.menu_select_pressed = pending_menu_select;
        pending_menu_toggle = false;
        pending_menu_up = false;
        pending_menu_down = false;
        pending_menu_select = false;

        GuiMenuActions actions{};
        gui_menu.handle_input(menu_input, devhud, noclip, actions);
        if (actions.ui_move_sfx && audio_ready) {
            ui_audio.play_move();
        }
        if (actions.ui_select_sfx && audio_ready) {
            ui_audio.play_click();
        }
        if (actions.toggle_devhud) {
            devhud = !devhud;
        }
        if (actions.toggle_noclip) {
            noclip = !noclip;
        }
        if (actions.start_game) {
            gameplay_started = true;
        }
        if (actions.host_local) {
            gameplay_started = true;
            host_local_secure();
        }
        if (actions.join_local) {
            gameplay_started = true;
            join_nearby_secure();
        }
        if (actions.host_lan) {
            gameplay_started = true;
            host_lan_secure();
        }
        if (actions.join_nearby) {
            gameplay_started = true;
            join_nearby_secure();
        }
        if (actions.reset_camera) {
            cam_yaw = 3.14159f;
            cam_pitch = -0.25f;
            camera_distance = 5.0f;
        }

        const bool menu_open = gui_menu.open();
        const bool menu_changed = (menu_open != menu_open_prev) ||
                                  menu_input.menu_toggle_pressed ||
                                  menu_input.menu_up_pressed ||
                                  menu_input.menu_down_pressed ||
                                  menu_input.menu_select_pressed;
        if (menu_changed) {
            const std::string menu_text = gui_menu.build_text(devhud, noclip, multiplayer_hint);
            __android_log_print(
                ANDROID_LOG_INFO,
                kLogTag,
                "GUI state: open=%d devhud=%d noclip=%d menu=\"%s\"",
                menu_open ? 1 : 0,
                devhud ? 1 : 0,
                noclip ? 1 : 0,
                menu_text.c_str());
        }
        menu_open_prev = menu_open;
    }

    void update_player_and_camera(double dt_seconds) {
        if (!gameplay_started) {
            touch.left_value = glm::vec2(0.0f);
            touch.look_delta = glm::vec2(0.0f);
            return;
        }
        const float look_scale = 0.0035f;
        if (gui_menu.open()) {
            touch.left_value = glm::vec2(0.0f);
            touch.look_delta = glm::vec2(0.0f);
        }
        cam_yaw += touch.look_delta.x * look_scale;
        cam_pitch += touch.look_delta.y * look_scale;
        cam_pitch = std::clamp(cam_pitch, -1.2f, 1.2f);
        touch.look_delta = glm::vec2(0.0f);

        const glm::vec3 forward_flat = glm::normalize(glm::vec3(std::sin(cam_yaw), 0.0f, -std::cos(cam_yaw)));
        const glm::vec3 right_flat = glm::normalize(glm::cross(forward_flat, glm::vec3(0.0f, 1.0f, 0.0f)));
        const float speed = 8.0f;
        const glm::vec3 move_delta = (forward_flat * touch.left_value.y + right_flat * touch.left_value.x) * speed * static_cast<float>(dt_seconds);

        if (noclip) {
            player_feet_position += move_delta;
            player_vertical_velocity = 0.0f;
            player_grounded = false;
        } else {
            if (!player_grounded) {
                player_vertical_velocity += -24.0f * static_cast<float>(dt_seconds);
            }

            glm::vec3 next_position = player_feet_position;
            next_position += move_delta;
            next_position.y += player_vertical_velocity * static_cast<float>(dt_seconds);

            const CapsuleResolveResult resolve = collision_world.resolve_capsule(
                next_position,
                player_capsule_radius,
                player_capsule_height,
                0.02f,
                8,
                1.2f);
            player_feet_position = resolve.position;
            player_grounded = resolve.grounded;
            if (player_grounded && player_vertical_velocity < 0.0f) {
                player_vertical_velocity = 0.0f;
            }
        }

        const glm::vec3 pivot = player_feet_position + glm::vec3(0.0f, camera_pivot_height, 0.0f);
        const glm::vec3 orbit_forward = glm::normalize(glm::vec3(
            std::cos(cam_pitch) * std::sin(cam_yaw),
            std::sin(cam_pitch),
            -std::cos(cam_pitch) * std::cos(cam_yaw)));
        cam_pos = pivot - orbit_forward * camera_distance;
    }

    void draw_mesh(const GpuMesh &mesh, const glm::mat4 &mvp) {
        if (mesh.vbo == 0 || mesh.ibo == 0 || mesh.index_count <= 0) {
            return;
        }

        glUseProgram(program);
        glUniformMatrix4fv(u_mvp, 1, GL_FALSE, glm::value_ptr(mvp));

        glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ibo);
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(RenderVertex), reinterpret_cast<void *>(offsetof(RenderVertex, position)));
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(RenderVertex), reinterpret_cast<void *>(offsetof(RenderVertex, color)));
        glDrawElements(GL_TRIANGLES, mesh.index_count, mesh.index_type, nullptr);
        glDisableVertexAttribArray(0);
        glDisableVertexAttribArray(1);
    }

    void draw_ui_overlay() {
        if (width <= 0 || height <= 0) {
            return;
        }
        auto draw_rect = [&](int x, int y_top, int w, int h, float r, float g, float b) {
            if (w <= 0 || h <= 0) {
                return;
            }
            const int sx = std::clamp(x, 0, width - 1);
            const int ex = std::clamp(x + w, 0, width);
            const int y_bottom = height - (y_top + h);
            const int sy = std::clamp(y_bottom, 0, height - 1);
            const int ey = std::clamp(y_bottom + h, 0, height);
            const int sw = ex - sx;
            const int sh = ey - sy;
            if (sw <= 0 || sh <= 0) {
                return;
            }
            glScissor(sx, sy, sw, sh);
            glClearColor(r, g, b, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
        };

        glDisable(GL_DEPTH_TEST);
        glEnable(GL_SCISSOR_TEST);

        draw_rect(18, 18, 120, 64, 0.18f, 0.24f, 0.32f);
        draw_rect(22, 22, 112, 56, 0.12f, 0.17f, 0.24f);

        if (gui_menu.open()) {
            const int panel_x = 20;
            const int panel_y = 88;
            const int panel_w = std::min(560, width - 40);
            const int panel_h = std::min(640, height - 120);
            draw_rect(panel_x, panel_y, panel_w, panel_h, 0.06f, 0.08f, 0.12f);
            draw_rect(panel_x + 4, panel_y + 4, panel_w - 8, panel_h - 8, 0.09f, 0.11f, 0.16f);

            if (gui_menu.page_id() == GuiMenu::Page::Main) {
                const int cards = 3;
                const int gap = 12;
                const int card_w = (panel_w - 36 - (cards - 1) * gap) / cards;
                const int card_h = std::min(170, panel_h / 3);
                for (int i = 0; i < cards; ++i) {
                    const int x = panel_x + 18 + i * (card_w + gap);
                    const int y = panel_y + 24;
                    const bool selected = (i == gui_menu.selected());
                    if (selected) {
                        draw_rect(x, y, card_w, card_h, 0.22f, 0.32f, 0.46f);
                        draw_rect(x + 3, y + 3, card_w - 6, card_h - 6, 0.16f, 0.24f, 0.36f);
                    } else {
                        draw_rect(x, y, card_w, card_h, 0.12f, 0.17f, 0.26f);
                        draw_rect(x + 3, y + 3, card_w - 6, card_h - 6, 0.10f, 0.14f, 0.21f);
                    }
                }
                const int close_y = panel_y + card_h + 52;
                const bool close_selected = (gui_menu.selected() == 3);
                if (close_selected) {
                    draw_rect(panel_x + 18, close_y, panel_w - 36, 64, 0.20f, 0.28f, 0.40f);
                    draw_rect(panel_x + 22, close_y + 4, panel_w - 44, 56, 0.15f, 0.22f, 0.32f);
                } else {
                    draw_rect(panel_x + 22, close_y + 6, panel_w - 44, 52, 0.11f, 0.15f, 0.22f);
                }
            } else {
                const int row_count = gui_menu.count();
                const int row_h = 64;
                for (int i = 0; i < row_count; ++i) {
                    const int row_y = panel_y + 24 + i * (row_h + 10);
                    const bool selected = (i == gui_menu.selected());
                    if (selected) {
                        draw_rect(panel_x + 18, row_y, panel_w - 36, row_h, 0.20f, 0.28f, 0.40f);
                        draw_rect(panel_x + 22, row_y + 4, panel_w - 44, row_h - 8, 0.15f, 0.22f, 0.32f);
                    } else {
                        draw_rect(panel_x + 22, row_y + 6, panel_w - 44, row_h - 12, 0.11f, 0.15f, 0.22f);
                    }
                }
            }
        }

        glDisable(GL_SCISSOR_TEST);

        const std::string menu_text = gui_menu.open() ? gui_menu.build_text(devhud, noclip, multiplayer_hint) : std::string();
        if (menu_text != ui_text_cache) {
            destroy_mesh(ui_text_gpu);
            ui_text_mesh = RenderMesh{};
            if (!menu_text.empty()) {
                ui_text_mesh = build_overlay_text_mesh(
                    menu_text,
                    width,
                    height,
                    36.0f,
                    100.0f,
                    3.6f,
                    glm::vec3(0.93f, 0.95f, 0.99f),
                    1.25f);
                ui_text_gpu = upload_mesh(ui_text_mesh);
            }
            ui_text_cache = menu_text;
        }

        if (ui_text_gpu.vbo != 0 && ui_text_gpu.ibo != 0 && ui_text_gpu.index_count > 0) {
            draw_mesh(ui_text_gpu, glm::mat4(1.0f));
        }

        glEnable(GL_DEPTH_TEST);
    }

    void render_frame() {
        if (!can_render()) {
            return;
        }

        timespec now{};
        clock_gettime(CLOCK_MONOTONIC, &now);
        double dt_seconds = 1.0 / 60.0;
        if (has_last_time) {
            dt_seconds = static_cast<double>(now.tv_sec - last_time.tv_sec) +
                         static_cast<double>(now.tv_nsec - last_time.tv_nsec) * 1.0e-9;
            dt_seconds = std::clamp(dt_seconds, 1.0 / 240.0, 0.05);
        }
        last_time = now;
        has_last_time = true;

        process_gui_actions();
        pump_network(dt_seconds);
        update_player_and_camera(dt_seconds);

        glViewport(0, 0, width, height);
        if (gui_menu.open()) {
            glClearColor(0.06f, 0.07f, 0.1f, 1.0f);
        } else {
            glClearColor(0.08f, 0.1f, 0.14f, 1.0f);
        }
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        const float aspect = (height > 0) ? static_cast<float>(width) / static_cast<float>(height) : 1.0f;
        const glm::vec3 pivot = player_feet_position + glm::vec3(0.0f, camera_pivot_height, 0.0f);
        const glm::mat4 view = glm::lookAt(cam_pos, pivot, glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 proj = glm::perspective(glm::radians(70.0f), aspect, 0.1f, 2000.0f);
        const glm::mat4 mvp = proj * view;

        draw_mesh(terrain_gpu, mvp);
        draw_mesh(grid_gpu, mvp);
        const glm::mat4 capsule_model = glm::translate(glm::mat4(1.0f), player_feet_position);
        draw_mesh(capsule_gpu, mvp * capsule_model);
        draw_ui_overlay();

        if (eglSwapBuffers(display, surface) == EGL_FALSE) {
            __android_log_print(ANDROID_LOG_ERROR, kLogTag, "eglSwapBuffers failed: %s", egl_error_to_string(eglGetError()));
            shutdown();
            return;
        }

        ++frame_counter;
        if (frame_counter == 1 || frame_counter % 300 == 0) {
            __android_log_print(
                ANDROID_LOG_INFO,
                kLogTag,
                "frame=%llu dt=%.3fms player=(%.2f,%.2f,%.2f) cam=(%.2f,%.2f,%.2f) move=(%.2f,%.2f)",
                static_cast<unsigned long long>(frame_counter),
                dt_seconds * 1000.0,
                player_feet_position.x,
                player_feet_position.y,
                player_feet_position.z,
                cam_pos.x,
                cam_pos.y,
                cam_pos.z,
                touch.left_value.x,
                touch.left_value.y);
        }
    }

    static float clamp_unit(float x) {
        return std::clamp(x, -1.0f, 1.0f);
    }

    int32_t on_input(android_app *app, AInputEvent *event) {
        if (event == nullptr || AInputEvent_getType(event) != AINPUT_EVENT_TYPE_MOTION) {
            return 0;
        }

        const int32_t action = AMotionEvent_getAction(event);
        const int32_t masked = action & AMOTION_EVENT_ACTION_MASK;
        const int32_t action_index = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
        const int32_t pointer_count = AMotionEvent_getPointerCount(event);

        auto assign_pointer = [&](int32_t pointer_id, float x, float y) {
            if (x < static_cast<float>(width) * 0.5f) {
                if (touch.left_pointer == -1) {
                    touch.left_pointer = pointer_id;
                    touch.left_origin = glm::vec2(x, y);
                    touch.left_value = glm::vec2(0.0f);
                }
            } else {
                if (touch.right_pointer == -1) {
                    touch.right_pointer = pointer_id;
                    touch.right_prev = glm::vec2(x, y);
                }
            }
        };

        if (masked == AMOTION_EVENT_ACTION_DOWN || masked == AMOTION_EVENT_ACTION_POINTER_DOWN) {
            const int32_t pointer_id = AMotionEvent_getPointerId(event, action_index);
            const float x = AMotionEvent_getX(event, action_index);
            const float y = AMotionEvent_getY(event, action_index);
            if (x <= 140.0f && y <= 140.0f) {
                pending_menu_toggle = true;
                return 1;
            }
            if (gui_menu.open()) {
                const int panel_x = 20;
                const int panel_y = 88;
                const int panel_w = std::min(560, width - 40);
                const int panel_h = std::min(640, height - 120);
                const bool inside_panel =
                    (x >= static_cast<float>(panel_x) && x <= static_cast<float>(panel_x + panel_w) &&
                     y >= static_cast<float>(panel_y) && y <= static_cast<float>(panel_y + panel_h));
                if (inside_panel) {
                    if (gui_menu.page_id() == GuiMenu::Page::Main) {
                        const int cards = 3;
                        const int gap = 12;
                        const int card_w = (panel_w - 36 - (cards - 1) * gap) / cards;
                        const int card_h = std::min(170, panel_h / 3);
                        for (int i = 0; i < cards; ++i) {
                            const int cx = panel_x + 18 + i * (card_w + gap);
                            const int cy = panel_y + 24;
                            if (x >= cx && x <= cx + card_w && y >= cy && y <= cy + card_h) {
                                gui_menu.set_selected(i);
                                pending_menu_select = true;
                                return 1;
                            }
                        }
                        const int close_y = panel_y + card_h + 52;
                        if (x >= panel_x + 18 && x <= panel_x + panel_w - 18 &&
                            y >= close_y && y <= close_y + 64) {
                            gui_menu.set_selected(3);
                            pending_menu_select = true;
                            return 1;
                        }
                    } else {
                        const int row_h = 64;
                        const int row_gap = 10;
                        const int row_start_y = panel_y + 24;
                        const int row_count = gui_menu.count();
                        const float local_y = y - static_cast<float>(row_start_y);
                        if (local_y >= 0.0f) {
                            const float row_span = static_cast<float>(row_h + row_gap);
                            const int tapped_row = static_cast<int>(local_y / row_span);
                            if (tapped_row >= 0 && tapped_row < row_count) {
                                const float in_row_y = local_y - static_cast<float>(tapped_row) * row_span;
                                if (in_row_y <= static_cast<float>(row_h)) {
                                    gui_menu.set_selected(tapped_row);
                                    pending_menu_select = true;
                                    return 1;
                                }
                            }
                        }
                    }
                }
                if (x < static_cast<float>(width) * 0.5f) {
                    if (y < static_cast<float>(height) * 0.5f) {
                        pending_menu_up = true;
                    } else {
                        pending_menu_down = true;
                    }
                } else {
                    pending_menu_select = true;
                }
                return 1;
            }
            assign_pointer(pointer_id, x, y);
        }

        if (masked == AMOTION_EVENT_ACTION_UP || masked == AMOTION_EVENT_ACTION_POINTER_UP || masked == AMOTION_EVENT_ACTION_CANCEL) {
            const int32_t pointer_id = AMotionEvent_getPointerId(event, action_index);
            if (touch.left_pointer == pointer_id) {
                touch.left_pointer = -1;
                touch.left_value = glm::vec2(0.0f);
            }
            if (touch.right_pointer == pointer_id) {
                touch.right_pointer = -1;
            }
        }

        if (masked == AMOTION_EVENT_ACTION_MOVE) {
            for (int32_t i = 0; i < pointer_count; ++i) {
                const int32_t pointer_id = AMotionEvent_getPointerId(event, i);
                const float x = AMotionEvent_getX(event, i);
                const float y = AMotionEvent_getY(event, i);

                if (pointer_id == touch.left_pointer) {
                    glm::vec2 delta = glm::vec2(x, y) - touch.left_origin;
                    const float radius = 140.0f;
                    if (glm::length(delta) > radius) {
                        delta = glm::normalize(delta) * radius;
                    }
                    touch.left_value.x = clamp_unit(delta.x / radius);
                    touch.left_value.y = clamp_unit(-delta.y / radius);
                }

                if (pointer_id == touch.right_pointer) {
                    const glm::vec2 pos(x, y);
                    touch.look_delta += (pos - touch.right_prev);
                    touch.right_prev = pos;
                }
            }
        }

        (void)app;
        return 1;
    }
};

void handle_app_cmd(android_app *app, int32_t cmd) {
    auto *renderer = reinterpret_cast<AndroidRenderer *>(app->userData);
    if (renderer == nullptr) {
        return;
    }

    switch (cmd) {
    case APP_CMD_INIT_WINDOW:
        __android_log_print(ANDROID_LOG_INFO, kLogTag, "APP_CMD_INIT_WINDOW");
        renderer->initialize(app);
        break;
    case APP_CMD_TERM_WINDOW:
        __android_log_print(ANDROID_LOG_INFO, kLogTag, "APP_CMD_TERM_WINDOW");
        renderer->shutdown();
        break;
    case APP_CMD_GAINED_FOCUS:
        __android_log_print(ANDROID_LOG_INFO, kLogTag, "APP_CMD_GAINED_FOCUS");
        renderer->focused = true;
        break;
    case APP_CMD_LOST_FOCUS:
        __android_log_print(ANDROID_LOG_INFO, kLogTag, "APP_CMD_LOST_FOCUS");
        renderer->focused = false;
        break;
    default:
        break;
    }
}

int32_t handle_input(android_app *app, AInputEvent *event) {
    auto *renderer = reinterpret_cast<AndroidRenderer *>(app->userData);
    if (renderer == nullptr) {
        return 0;
    }
    return renderer->on_input(app, event);
}
}

void android_main(android_app *app) {
    app_dummy();

    AndroidRenderer renderer{};
    app->userData = &renderer;
    app->onAppCmd = handle_app_cmd;
    app->onInputEvent = handle_input;
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "android_main started");

    while (true) {
        int events = 0;
        android_poll_source *source = nullptr;
        const int timeout_ms = renderer.can_render() ? 0 : -1;
        while (ALooper_pollOnce(timeout_ms, nullptr, &events, reinterpret_cast<void **>(&source)) >= 0) {
            if (source) {
                source->process(app, source);
            }

            if (app->destroyRequested != 0) {
                renderer.shutdown();
                __android_log_print(ANDROID_LOG_INFO, kLogTag, "android_main exit");
                return;
            }

            if (renderer.can_render()) {
                break;
            }
        }

        if (!renderer.can_render() && app->window != nullptr) {
            renderer.initialize(app);
        }

        if (renderer.can_render()) {
            renderer.render_frame();
        }
    }
}
