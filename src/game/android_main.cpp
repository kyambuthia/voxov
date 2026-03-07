#include "engine_world/voxel_chunk.hpp"
#include "engine_world/physics/voxel_collision.hpp"
#include "engine_assets/skinned_model.hpp"
#include "engine_gameplay/animation/skeletal_animator.hpp"
#include "engine_gameplay/player/player_controller.hpp"
#include "engine_gameplay/player/player_visuals.hpp"
#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include "engine_render/debug_draw/debug_draw.hpp"
#include "engine_ui/gui_menu.hpp"
#include "engine_input/input_state.hpp"
#include "engine_audio/ui_audio.hpp"
#include "engine_net/net_client.hpp"
#include "engine_net/lan_discovery.hpp"
#include "engine_net/remote_interp.hpp"
#include "engine_net/net_runtime_shared.hpp"
#include "engine_net/net_server.hpp"
#include "engine_world/world_gen.hpp"

#include <android/input.h>
#include <android/log.h>
#include <android/asset_manager.h>
#include <android/configuration.h>
#include <android_native_app_glue.h>
#include <jni.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <deque>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <fstream>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>

namespace {
constexpr const char *kLogTag = "VOXOV";
constexpr uint16_t kLocalPlayPort = 7777;
constexpr const char *kLocalPlayHost = "127.0.0.1";
constexpr double kConnectTimeoutSeconds = 5.0;
constexpr uint32_t kRemoteInterpDelayTicks = 6;
constexpr size_t kRemoteSampleHistoryMax = 16;

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

glm::vec3 player_color_from_id(uint32_t player_id) {
    return player_color_from_network_id(player_id);
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

class AndroidMulticastLock {
public:
    void set_activity(android_app *app_state) {
        app = app_state;
    }

    void acquire() {
        if (held) {
            return;
        }
        bool attached = false;
        JNIEnv *env = attach_env(attached);
        if (!env) {
            return;
        }
        if (!ensure_lock(env)) {
            detach_if_needed(attached);
            return;
        }
        if (call_lock_method(env, "acquire", "()V")) {
            held = true;
            __android_log_print(ANDROID_LOG_INFO, kLogTag, "Android multicast lock acquired");
        }
        detach_if_needed(attached);
    }

    void release() {
        if (!held && lock_global == nullptr) {
            return;
        }
        bool attached = false;
        JNIEnv *env = attach_env(attached);
        if (!env) {
            return;
        }
        if (lock_global != nullptr && held) {
            if (call_lock_method(env, "release", "()V")) {
                __android_log_print(ANDROID_LOG_INFO, kLogTag, "Android multicast lock released");
            }
        }
        held = false;
        detach_if_needed(attached);
    }

    void shutdown() {
        release();

        bool attached = false;
        JNIEnv *env = attach_env(attached);
        if (!env) {
            lock_global = nullptr;
            wifi_manager_global = nullptr;
            return;
        }
        if (lock_global != nullptr) {
            env->DeleteGlobalRef(lock_global);
            lock_global = nullptr;
        }
        if (wifi_manager_global != nullptr) {
            env->DeleteGlobalRef(wifi_manager_global);
            wifi_manager_global = nullptr;
        }
        detach_if_needed(attached);
    }

private:
    JNIEnv *attach_env(bool &attached) const {
        attached = false;
        if (!app || !app->activity || !app->activity->vm) {
            return nullptr;
        }
        JNIEnv *env = nullptr;
        if (app->activity->vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) == JNI_OK) {
            return env;
        }
        if (app->activity->vm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
            __android_log_print(ANDROID_LOG_ERROR, kLogTag, "AttachCurrentThread failed for multicast lock");
            return nullptr;
        }
        attached = true;
        return env;
    }

    void detach_if_needed(bool attached) const {
        if (!attached || !app || !app->activity || !app->activity->vm) {
            return;
        }
        app->activity->vm->DetachCurrentThread();
    }

    bool ensure_lock(JNIEnv *env) {
        if (!env || lock_global != nullptr) {
            return lock_global != nullptr;
        }
        if (!app || !app->activity || app->activity->clazz == nullptr) {
            return false;
        }

        jobject activity = app->activity->clazz;
        jclass activity_cls = env->GetObjectClass(activity);
        if (!activity_cls) {
            return false;
        }
        jmethodID get_system_service = env->GetMethodID(
            activity_cls,
            "getSystemService",
            "(Ljava/lang/String;)Ljava/lang/Object;");
        env->DeleteLocalRef(activity_cls);
        if (!get_system_service) {
            return false;
        }

        jstring wifi_service = env->NewStringUTF("wifi");
        jobject wifi_manager_local = env->CallObjectMethod(activity, get_system_service, wifi_service);
        env->DeleteLocalRef(wifi_service);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            return false;
        }
        if (!wifi_manager_local) {
            return false;
        }

        jclass wifi_cls = env->GetObjectClass(wifi_manager_local);
        if (!wifi_cls) {
            env->DeleteLocalRef(wifi_manager_local);
            return false;
        }
        jmethodID create_lock = env->GetMethodID(
            wifi_cls,
            "createMulticastLock",
            "(Ljava/lang/String;)Landroid/net/wifi/WifiManager$MulticastLock;");
        env->DeleteLocalRef(wifi_cls);
        if (!create_lock) {
            env->DeleteLocalRef(wifi_manager_local);
            return false;
        }

        jstring lock_name = env->NewStringUTF("voxov_lan_discovery");
        jobject lock_local = env->CallObjectMethod(wifi_manager_local, create_lock, lock_name);
        env->DeleteLocalRef(lock_name);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            env->DeleteLocalRef(wifi_manager_local);
            return false;
        }
        if (!lock_local) {
            env->DeleteLocalRef(wifi_manager_local);
            return false;
        }

        jclass lock_cls = env->GetObjectClass(lock_local);
        if (lock_cls) {
            jmethodID set_ref_counted = env->GetMethodID(lock_cls, "setReferenceCounted", "(Z)V");
            if (set_ref_counted) {
                env->CallVoidMethod(lock_local, set_ref_counted, JNI_FALSE);
                if (env->ExceptionCheck()) {
                    env->ExceptionClear();
                }
            }
            env->DeleteLocalRef(lock_cls);
        }

        wifi_manager_global = env->NewGlobalRef(wifi_manager_local);
        lock_global = env->NewGlobalRef(lock_local);
        env->DeleteLocalRef(wifi_manager_local);
        env->DeleteLocalRef(lock_local);
        return lock_global != nullptr;
    }

    bool call_lock_method(JNIEnv *env, const char *method_name, const char *signature) {
        if (!env || !lock_global || !method_name || !signature) {
            return false;
        }
        jclass lock_cls = env->GetObjectClass(lock_global);
        if (!lock_cls) {
            return false;
        }
        jmethodID method = env->GetMethodID(lock_cls, method_name, signature);
        env->DeleteLocalRef(lock_cls);
        if (!method) {
            return false;
        }
        env->CallVoidMethod(lock_global, method);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            return false;
        }
        return true;
    }

    android_app *app = nullptr;
    jobject wifi_manager_global = nullptr;
    jobject lock_global = nullptr;
    bool held = false;
};

struct TouchState {
    int32_t left_pointer = -1;
    int32_t right_pointer = -1;
    int32_t jump_pointer = -1;
    int32_t sprint_pointer = -1;
    int32_t crouch_pointer = -1;
    glm::vec2 left_origin = glm::vec2(0.0f);
    glm::vec2 left_value = glm::vec2(0.0f);
    glm::vec2 right_prev = glm::vec2(0.0f);
    glm::vec2 look_delta = glm::vec2(0.0f);
    bool jump_held = false;
    bool jump_pressed = false;
    bool sprint_held = false;
    bool crouch_held = false;
};

struct GpuMesh {
    GLuint vbo = 0;
    GLuint ibo = 0;
    GLsizei index_count = 0;
    GLenum index_type = GL_UNSIGNED_INT;
};

struct AndroidRenderer {
    struct RemoteRenderPlayer {
        struct Sample {
            uint32_t server_tick = 0;
            glm::vec3 position = glm::vec3(0.0f);
            glm::vec3 velocity = glm::vec3(0.0f);
            uint8_t anim_state = 0;
            float anim_phase = 0.0f;
            float anim_blend = 0.0f;
        };
        glm::vec3 position = glm::vec3(0.0f);
        glm::vec3 target_position = glm::vec3(0.0f);
        glm::vec3 velocity = glm::vec3(0.0f);
        uint8_t anim_state = 0;
        float anim_phase = 0.0f;
        float anim_blend = 0.0f;
        bool initialized = false;
        std::deque<Sample> samples;
    };

    struct UiRect {
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;
    };

    struct UiSafeArea {
        int left = 0;
        int top = 0;
        int right = 0;
        int bottom = 0;
    };

    struct UiMenuLayout {
        UiRect panel{};
        int outer_inset = 18;
        int inner_inset = 22;
        int title_y = 0;
        int guide_y = 0;
        int guide_step = 24;
        int row_start_y = 0;
        int row_h = 64;
        int row_gap = 10;
        int status_y = 0;
        float title_px = 3.0f;
        float guide_px = 2.4f;
        float item_px = 2.7f;
        float status_px = 2.0f;
    };

    struct UiTouchLayout {
        bool tablet = false;
        int menu_w = 112;
        int menu_h = 56;
        int action_w = 128;
        int action_h = 88;
        int action_gap = 12;
        int menu_hit_margin = 16;
        int action_hit_margin = 18;
        float menu_label_px = 2.8f;
        float action_label_px = 3.0f;
    };

    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
    int32_t width = 0;
    int32_t height = 0;
    bool focused = false;
    int gles_version = 0;
    bool can_draw_uint_indices = false;
    android_app *app = nullptr;

    GLuint program = 0;
    GLint u_mvp = -1;
    GpuMesh terrain_gpu{};
    GpuMesh grid_gpu{};
    GpuMesh capsule_gpu{};
    GpuMesh remote_players_gpu{};
    GpuMesh ui_text_gpu{};

    VoxelChunk world{};
    NetChunkState streamed_world_state = net_make_flat_chunk_state(NetChunkCoord{});
    RenderMesh terrain_mesh{};
    RenderMesh grid_mesh{};
    RenderMesh capsule_mesh{};
    RenderMesh remote_players_mesh{};
    RenderMesh ui_text_mesh{};
    VoxelCollisionWorld collision_world{nullptr};
    glm::vec3 player_feet_position = glm::vec3(8.5f, 6.0f, 8.5f);
    float player_vertical_velocity = 0.0f;
    bool player_grounded = false;
    float player_capsule_radius = 0.45f;
    float player_capsule_height = 1.8f;
    PlayerAnimState player_anim_state = PlayerAnimState::Idle;
    float player_anim_phase = 0.0f;
    float player_anim_blend = 0.0f;
    glm::quat player_anim_orientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    SkinnedModel humanoid_player_model{};
    bool has_humanoid_player_model = false;

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
    AndroidMulticastLock multicast_lock{};
    bool net_initialized = false;
    bool net_connected = false;
    bool net_connecting = false;
    double net_connect_elapsed = 0.0;
    NetClientConnectionState last_net_connection_state = NetClientConnectionState::Disconnected;
    bool local_server_running = false;
    bool local_server_loopback = true;
    bool hosting_local = false;
    bool searching_nearby = false;
    uint32_t net_tick = 0;
    uint32_t net_local_player_id = 0;
    glm::vec3 net_target_position = glm::vec3(8.5f, 6.0f, 8.5f);
    bool net_target_valid = false;
    float net_reconcile_error = 0.0f;
    uint32_t net_last_snapshot_tick = 0;
    uint64_t net_reconcile_corrections = 0;
    std::unordered_map<uint32_t, RemoteRenderPlayer> remote_render_players;
    std::string ui_text_cache;
    std::string multiplayer_hint;

    timespec last_time{};
    bool has_last_time = false;
    uint64_t frame_counter = 0;
    double remote_interp_tick_cursor = 0.0;
    bool remote_interp_tick_cursor_initialized = false;
    mutable UiSafeArea cached_safe_area{};
    mutable bool cached_safe_area_valid = false;

    void refresh_multicast_lock_state() {
        const bool needs_multicast = searching_nearby || (local_server_running && !local_server_loopback);
        if (needs_multicast) {
            multicast_lock.acquire();
        } else {
            multicast_lock.release();
        }
    }

    JNIEnv *attach_env(bool &attached) const {
        attached = false;
        if (!app || !app->activity || !app->activity->vm) {
            return nullptr;
        }
        JNIEnv *env = nullptr;
        if (app->activity->vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) == JNI_OK) {
            return env;
        }
        if (app->activity->vm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
            __android_log_print(ANDROID_LOG_ERROR, kLogTag, "AttachCurrentThread failed for safe area query");
            return nullptr;
        }
        attached = true;
        return env;
    }

    void detach_env(bool attached) const {
        if (!attached || !app || !app->activity || !app->activity->vm) {
            return;
        }
        app->activity->vm->DetachCurrentThread();
    }

    UiSafeArea fallback_safe_area() const {
        const int shortest_edge = std::max(1, std::min(width, height));
        const int side_margin = std::max(18, shortest_edge / 28);
        const int top_margin = std::max(26, shortest_edge / 18);
        const int bottom_margin = std::max(20, shortest_edge / 24);
        return UiSafeArea{side_margin, top_margin, side_margin, bottom_margin};
    }

    UiSafeArea query_platform_safe_area() const {
        UiSafeArea safe = fallback_safe_area();
        bool attached = false;
        JNIEnv *env = attach_env(attached);
        if (!env || !app || !app->activity || app->activity->clazz == nullptr) {
            detach_env(attached);
            return safe;
        }

        jobject activity = app->activity->clazz;
        jclass activity_cls = env->GetObjectClass(activity);
        if (!activity_cls) {
            detach_env(attached);
            return safe;
        }

        const jmethodID get_window = env->GetMethodID(activity_cls, "getWindow", "()Landroid/view/Window;");
        env->DeleteLocalRef(activity_cls);
        if (!get_window) {
            detach_env(attached);
            return safe;
        }

        jobject window = env->CallObjectMethod(activity, get_window);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            detach_env(attached);
            return safe;
        }
        if (!window) {
            detach_env(attached);
            return safe;
        }

        jclass window_cls = env->GetObjectClass(window);
        const jmethodID get_decor_view = window_cls
            ? env->GetMethodID(window_cls, "getDecorView", "()Landroid/view/View;")
            : nullptr;
        if (window_cls) {
            env->DeleteLocalRef(window_cls);
        }
        if (!get_decor_view) {
            env->DeleteLocalRef(window);
            detach_env(attached);
            return safe;
        }

        jobject decor_view = env->CallObjectMethod(window, get_decor_view);
        env->DeleteLocalRef(window);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            detach_env(attached);
            return safe;
        }
        if (!decor_view) {
            detach_env(attached);
            return safe;
        }

        jclass view_cls = env->GetObjectClass(decor_view);
        const jmethodID get_root_window_insets = view_cls
            ? env->GetMethodID(view_cls, "getRootWindowInsets", "()Landroid/view/WindowInsets;")
            : nullptr;
        if (view_cls) {
            env->DeleteLocalRef(view_cls);
        }
        if (!get_root_window_insets) {
            env->DeleteLocalRef(decor_view);
            detach_env(attached);
            return safe;
        }

        jobject insets = env->CallObjectMethod(decor_view, get_root_window_insets);
        env->DeleteLocalRef(decor_view);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            detach_env(attached);
            return safe;
        }
        if (!insets) {
            detach_env(attached);
            return safe;
        }

        jclass insets_cls = env->GetObjectClass(insets);
        if (!insets_cls) {
            env->DeleteLocalRef(insets);
            detach_env(attached);
            return safe;
        }

        const jmethodID inset_left = env->GetMethodID(insets_cls, "getSystemWindowInsetLeft", "()I");
        const jmethodID inset_top = env->GetMethodID(insets_cls, "getSystemWindowInsetTop", "()I");
        const jmethodID inset_right = env->GetMethodID(insets_cls, "getSystemWindowInsetRight", "()I");
        const jmethodID inset_bottom = env->GetMethodID(insets_cls, "getSystemWindowInsetBottom", "()I");
        int left = 0;
        int top = 0;
        int right = 0;
        int bottom = 0;
        if (inset_left && inset_top && inset_right && inset_bottom) {
            left = std::max(0, static_cast<int>(env->CallIntMethod(insets, inset_left)));
            top = std::max(0, static_cast<int>(env->CallIntMethod(insets, inset_top)));
            right = std::max(0, static_cast<int>(env->CallIntMethod(insets, inset_right)));
            bottom = std::max(0, static_cast<int>(env->CallIntMethod(insets, inset_bottom)));
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                left = top = right = bottom = 0;
            }
        }

        const jmethodID get_display_cutout = env->GetMethodID(insets_cls, "getDisplayCutout", "()Landroid/view/DisplayCutout;");
        env->DeleteLocalRef(insets_cls);
        if (get_display_cutout) {
            jobject cutout = env->CallObjectMethod(insets, get_display_cutout);
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
            } else if (cutout) {
                jclass cutout_cls = env->GetObjectClass(cutout);
                const jmethodID safe_left = cutout_cls ? env->GetMethodID(cutout_cls, "getSafeInsetLeft", "()I") : nullptr;
                const jmethodID safe_top = cutout_cls ? env->GetMethodID(cutout_cls, "getSafeInsetTop", "()I") : nullptr;
                const jmethodID safe_right = cutout_cls ? env->GetMethodID(cutout_cls, "getSafeInsetRight", "()I") : nullptr;
                const jmethodID safe_bottom = cutout_cls ? env->GetMethodID(cutout_cls, "getSafeInsetBottom", "()I") : nullptr;
                if (safe_left && safe_top && safe_right && safe_bottom) {
                    left = std::max(left, std::max(0, static_cast<int>(env->CallIntMethod(cutout, safe_left))));
                    top = std::max(top, std::max(0, static_cast<int>(env->CallIntMethod(cutout, safe_top))));
                    right = std::max(right, std::max(0, static_cast<int>(env->CallIntMethod(cutout, safe_right))));
                    bottom = std::max(bottom, std::max(0, static_cast<int>(env->CallIntMethod(cutout, safe_bottom))));
                    if (env->ExceptionCheck()) {
                        env->ExceptionClear();
                    }
                }
                if (cutout_cls) {
                    env->DeleteLocalRef(cutout_cls);
                }
                env->DeleteLocalRef(cutout);
            }
        }

        env->DeleteLocalRef(insets);
        detach_env(attached);

        // Keep a baseline margin even when the platform insets are zero so the GUI
        // still breathes on rectangular displays.
        safe.left = std::max(safe.left, left + 8);
        safe.top = std::max(safe.top, top + 8);
        safe.right = std::max(safe.right, right + 8);
        safe.bottom = std::max(safe.bottom, bottom + 8);
        return safe;
    }

    void invalidate_safe_area() {
        cached_safe_area_valid = false;
    }

    UiSafeArea ui_safe_area() const {
        if (!cached_safe_area_valid) {
            cached_safe_area = query_platform_safe_area();
            cached_safe_area_valid = true;
        }
        return cached_safe_area;
    }

    UiRect menu_button_rect() const {
        const UiTouchLayout touch_layout_state = touch_layout();
        const UiSafeArea safe = ui_safe_area();
        const int w = touch_layout_state.menu_w;
        const int h = touch_layout_state.menu_h;
        return UiRect{safe.left, safe.top, w, h};
    }

    UiRect menu_panel_rect() const {
        const UiSafeArea safe = ui_safe_area();
        const UiRect menu_button = menu_button_rect();
        const int gap = std::max(14, std::min(width, height) / 36);
        const int x = safe.left;
        const int y = menu_button.y + menu_button.h + gap;
        const int avail_w = std::max(1, width - safe.left - safe.right);
        const int avail_h = std::max(1, height - y - safe.bottom);
        int w = std::min(560, std::max(220, avail_w));
        int h = std::min(640, std::max(240, avail_h));
        w = std::min(w, avail_w);
        h = std::min(h, avail_h);
        return UiRect{x, y, w, h};
    }

    UiMenuLayout menu_layout(const GuiMenuView &menu_view) const {
        UiMenuLayout layout{};
        layout.panel = menu_panel_rect();
        const int shortest_edge = std::max(1, std::min(width, height));
        layout.outer_inset = std::max(14, std::min(22, shortest_edge / 42));
        layout.inner_inset = layout.outer_inset + 4;
        layout.title_px = std::clamp(static_cast<float>(shortest_edge) / 280.0f, 2.6f, 3.2f);
        layout.guide_px = std::clamp(layout.title_px - 0.5f, 2.1f, 2.6f);
        layout.item_px = std::clamp(layout.title_px - 0.2f, 2.3f, 3.0f);
        layout.status_px = std::clamp(layout.title_px - 0.8f, 1.9f, 2.3f);

        const int title_block_h = std::max(26, std::min(40, layout.panel.h / 9));
        layout.title_y = layout.panel.y + layout.outer_inset;

        const int guide_count = static_cast<int>(menu_view.guide_lines.size());
        layout.guide_step = std::max(20, std::min(28, layout.panel.h / 20));
        const int guide_block_h = guide_count > 0 ? (guide_count * layout.guide_step + layout.outer_inset / 2) : 0;
        layout.guide_y = layout.title_y + title_block_h;

        const int status_block_h = menu_view.status.empty()
            ? 0
            : std::max(22, std::min(34, layout.panel.h / 11));

        const int row_count = std::max(1, static_cast<int>(menu_view.items.size()));
        layout.row_gap = std::max(8, std::min(14, layout.panel.h / 44));
        const int rows_top = layout.guide_y + guide_block_h;
        const int rows_bottom = layout.panel.y + layout.panel.h - layout.outer_inset - status_block_h;
        const int usable_rows_h = std::max(
            row_count * 36,
            rows_bottom - rows_top - std::max(0, row_count - 1) * layout.row_gap);
        const int target_row_h = (gui_menu.page_id() == GuiMenu::Page::Main) ? 84 : 70;
        layout.row_h = std::clamp(usable_rows_h / row_count, 38, target_row_h);
        layout.row_start_y = rows_top;
        layout.status_y = rows_bottom + std::max(8, layout.outer_inset / 2);
        return layout;
    }

    UiTouchLayout touch_layout() const {
        UiTouchLayout layout{};
        const int shortest_edge = std::max(1, std::min(width, height));
        const int longest_edge = std::max(width, height);
        int density_dpi = 0;
        if (app && app->config) {
            density_dpi = AConfiguration_getDensity(app->config);
        }

        if (density_dpi > 0 &&
            density_dpi != ACONFIGURATION_DENSITY_DEFAULT &&
            density_dpi != ACONFIGURATION_DENSITY_ANY &&
            density_dpi != ACONFIGURATION_DENSITY_NONE) {
            const float shortest_dp = static_cast<float>(shortest_edge) * 160.0f / static_cast<float>(density_dpi);
            const float longest_dp = static_cast<float>(longest_edge) * 160.0f / static_cast<float>(density_dpi);
            layout.tablet = shortest_dp >= 600.0f && longest_dp >= 900.0f;
        } else {
            layout.tablet = shortest_edge >= 900 || (shortest_edge >= 720 && longest_edge >= 1280);
        }

        if (layout.tablet) {
            layout.menu_w = std::max(136, std::min(188, width / 7));
            layout.menu_h = std::max(64, std::min(92, height / 12));
            layout.action_w = std::max(148, std::min(230, width / 5));
            layout.action_h = std::max(102, std::min(154, height / 6));
            layout.action_gap = std::max(16, shortest_edge / 52);
            layout.menu_hit_margin = std::max(18, shortest_edge / 72);
            layout.action_hit_margin = std::max(22, shortest_edge / 56);
            layout.menu_label_px = 3.0f;
            layout.action_label_px = 3.3f;
            return layout;
        }

        layout.menu_w = std::max(104, std::min(148, width / 5));
        layout.menu_h = std::max(52, std::min(72, height / 13));
        layout.action_w = std::max(108, std::min(178, width / 4));
        layout.action_h = std::max(78, std::min(126, height / 7));
        layout.action_gap = std::max(10, shortest_edge / 56);
        layout.menu_hit_margin = std::max(14, shortest_edge / 80);
        layout.action_hit_margin = std::max(18, shortest_edge / 62);
        layout.menu_label_px = 2.6f;
        layout.action_label_px = 2.9f;
        return layout;
    }

    UiRect jump_button_rect() const {
        const UiTouchLayout touch_layout_state = touch_layout();
        const UiSafeArea safe = ui_safe_area();
        const int w = touch_layout_state.action_w;
        const int h = touch_layout_state.action_h;
        return UiRect{width - safe.right - w, height - safe.bottom - h, w, h};
    }

    UiRect sprint_button_rect() const {
        const UiTouchLayout touch_layout_state = touch_layout();
        const UiRect jump = jump_button_rect();
        const int gap = touch_layout_state.action_gap;
        const int left = std::max(ui_safe_area().left, jump.x - jump.w - gap);
        return UiRect{left, jump.y, jump.w, jump.h};
    }

    UiRect crouch_button_rect() const {
        const UiTouchLayout touch_layout_state = touch_layout();
        const UiRect jump = jump_button_rect();
        const int gap = touch_layout_state.action_gap;
        const int top = std::max(ui_safe_area().top, jump.y - jump.h - gap);
        return UiRect{jump.x, top, jump.w, jump.h};
    }

    static bool rect_contains(const UiRect &r, float px, float py) {
        return px >= static_cast<float>(r.x) &&
               px <= static_cast<float>(r.x + r.w) &&
               py >= static_cast<float>(r.y) &&
               py <= static_cast<float>(r.y + r.h);
    }

    static bool rect_contains_margin(const UiRect &r, float px, float py, float margin) {
        return px >= static_cast<float>(r.x) - margin &&
               px <= static_cast<float>(r.x + r.w) + margin &&
               py >= static_cast<float>(r.y) - margin &&
               py <= static_cast<float>(r.y + r.h) + margin;
    }

    bool extract_asset_to_file(const char *asset_path, std::string &out_path) {
        if (!app || !app->activity || !app->activity->assetManager || !app->activity->internalDataPath || !asset_path) {
            return false;
        }
        AAssetManager *mgr = app->activity->assetManager;
        AAsset *asset = AAssetManager_open(mgr, asset_path, AASSET_MODE_STREAMING);
        if (!asset) {
            return false;
        }
        const off_t len = AAsset_getLength(asset);
        if (len <= 0) {
            AAsset_close(asset);
            return false;
        }

        std::vector<uint8_t> data(static_cast<size_t>(len));
        const int read_bytes = AAsset_read(asset, data.data(), static_cast<size_t>(len));
        AAsset_close(asset);
        if (read_bytes != len) {
            return false;
        }

        const char *leaf = std::strrchr(asset_path, '/');
        leaf = (leaf && *(leaf + 1) != '\0') ? (leaf + 1) : asset_path;
        out_path = std::string(app->activity->internalDataPath) + "/" + leaf;
        std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            return false;
        }
        out.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
        out.close();
        return out.good();
    }

    void init_audio_if_needed() {
        if (audio_ready) {
            return;
        }
        audio_ready = ui_audio.init();
    }

    void clear_touch_actions(bool clear_look_delta) {
        touch.left_value = glm::vec2(0.0f);
        if (clear_look_delta) {
            touch.look_delta = glm::vec2(0.0f);
        }
        touch.jump_pointer = -1;
        touch.sprint_pointer = -1;
        touch.crouch_pointer = -1;
        touch.jump_held = false;
        touch.jump_pressed = false;
        touch.sprint_held = false;
        touch.crouch_held = false;
    }

    void init_network_if_needed() {
        if (net_initialized) {
            return;
        }
        if (!net_client.init()) {
            multiplayer_hint = "Network init failed.";
            __android_log_print(ANDROID_LOG_ERROR, kLogTag, "NetClient init failed");
            return;
        }
        net_initialized = true;
        net_connected = false;
        net_connecting = false;
        net_connect_elapsed = 0.0;
        net_tick = 0;
        net_local_player_id = 0;
        remote_render_players.clear();
    }

    void stop_local_server() {
        if (!local_server_running) {
            return;
        }
        local_server.shutdown();
        local_server_running = false;
        local_server_loopback = true;
        hosting_local = false;
        refresh_multicast_lock_state();
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
        net_target_valid = false;
        net_local_player_id = 0;
        remote_render_players.clear();
        reset_streamed_world_state();
        refresh_multicast_lock_state();
    }

    void stop_session_transports() {
        stop_client();
        stop_local_server();
        lan_discovery.stop();
        searching_nearby = false;
        refresh_multicast_lock_state();
    }

    void leave_session_secure() {
        stop_session_transports();
        multiplayer_hint = "Left session.";
    }

    void shutdown_network() {
        stop_client();
        stop_local_server();
        lan_discovery.stop();
        searching_nearby = false;
        refresh_multicast_lock_state();
        if (net_initialized) {
            net_client.shutdown();
            net_initialized = false;
        }
    }

    void connect_local() {
        init_network_if_needed();
        if (!net_initialized) {
            return;
        }
        if (net_connected || net_connecting) {
            return;
        }

        if (!net_client.connect(kLocalPlayHost, kLocalPlayPort)) {
            multiplayer_hint = "Failed to connect local server.";
            __android_log_print(ANDROID_LOG_ERROR, kLogTag, "NetClient connect failed to local server");
            return;
        }
        net_target_valid = false;
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
        if (!net_initialized) {
            return;
        }
        stop_session_transports();
        if (!local_server.init(kLocalPlayPort, true)) {
            multiplayer_hint = "Failed to start local server.";
            __android_log_print(ANDROID_LOG_ERROR, kLogTag, "Local loopback server start failed on %u", kLocalPlayPort);
            return;
        }
        local_server_running = true;
        local_server_loopback = true;
        hosting_local = true;
        __android_log_print(ANDROID_LOG_INFO, kLogTag, "Loopback-only local server started on %u", kLocalPlayPort);
        multiplayer_hint = "Hosting this device only.";
        refresh_multicast_lock_state();
        connect_local();
    }

    void host_lan_secure() {
        init_network_if_needed();
        if (!net_initialized) {
            return;
        }
        stop_session_transports();
        if (!local_server.init(kLocalPlayPort, false)) {
            multiplayer_hint = "Failed to start Wi-Fi host.";
            __android_log_print(ANDROID_LOG_ERROR, kLogTag, "LAN server start failed on %u", kLocalPlayPort);
            return;
        }
        local_server_running = true;
        local_server_loopback = false;
        lan_discovery.start_host(kLocalPlayPort, "VOXOV Host");
        multiplayer_hint = "Hosting Wi-Fi game. Friends tap Join Nearby.";
        refresh_multicast_lock_state();
        connect_local();
    }

    void join_nearby_secure() {
        init_network_if_needed();
        if (!net_initialized) {
            return;
        }
        stop_session_transports();
        lan_discovery.start_client();
        searching_nearby = true;
        net_connect_elapsed = 0.0;
        multiplayer_hint = "Searching nearby Wi-Fi hosts...";
        refresh_multicast_lock_state();
    }

    std::string multiplayer_status_text() const {
        const NetClientConnectionState connection_state = net_client.connection_state();
        if (connection_state == NetClientConnectionState::Connected) {
            if (net_client.has_session_info()) {
                return net_session_status_line(net_client.session_info());
            }
            return "Connected to game server.";
        }
        if (searching_nearby) {
            return "Searching nearby Wi-Fi hosts...";
        }
        if (connection_state == NetClientConnectionState::Connecting) {
            const std::string &target_host = net_client.connect_target_host();
            if (!target_host.empty()) {
                return "Connecting to " + target_host + ":" + std::to_string(net_client.connect_target_port()) + "...";
            }
            return "Connecting...";
        }
        if (local_server_running) {
            return local_server_loopback ? "Hosting this device only." : "Hosting Wi-Fi game.";
        }
        return multiplayer_hint;
    }

    GuiSessionContext session_context() const {
        GuiSessionContext session{};
        session.connected = net_connected;
        session.connecting = net_connecting;
        session.searching = searching_nearby;
        session.hosting_local = local_server_running && local_server_loopback;
        session.hosting_lan = local_server_running && !local_server_loopback;
        session.can_leave = session.connected || session.connecting || session.searching ||
            session.hosting_local || session.hosting_lan;
        session.status = multiplayer_status_text();
        return session;
    }

    void pump_network(double dt_seconds) {
        refresh_multicast_lock_state();
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
            if (!net_connected) {
                net_target_valid = false;
                net_local_player_id = 0;
                remote_render_players.clear();
            }
            __android_log_print(ANDROID_LOG_INFO, kLogTag, "NetClient state changed: connected=%d", net_connected ? 1 : 0);
        }
        const NetClientConnectionState connection_state = net_client.connection_state();
        if (connection_state != last_net_connection_state) {
            if (connection_state == NetClientConnectionState::Connected) {
                multiplayer_hint.clear();
            } else if (last_net_connection_state == NetClientConnectionState::Connected &&
                       multiplayer_hint != "Left session.") {
                multiplayer_hint = "Disconnected from server.";
            }
            last_net_connection_state = connection_state;
        }

        if (!net_connected && net_connecting) {
            net_connect_elapsed += dt_seconds;
            if (net_connect_elapsed >= kConnectTimeoutSeconds) {
                __android_log_print(ANDROID_LOG_WARN, kLogTag, "NetClient connect timeout after %.2fs", net_connect_elapsed);
                stop_client();
                multiplayer_hint = "Connection timed out.";
            }
        }

        if (!net_connected) {
            if (searching_nearby) {
                lan_discovery.pump();
                LanHostEntry host{};
                if (lan_discovery.pop_host(host)) {
                    if (!net_client.connect(host.ip.c_str(), host.port)) {
                        multiplayer_hint = "Join failed. Retrying discovery...";
                        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "NetClient connect failed to discovered host %s:%u", host.ip.c_str(), host.port);
                        return;
                    }
                    NetChunkInterest interest{};
                    interest.center_x = 0;
                    interest.center_z = 0;
                    interest.radius = 2;
                    net_client.set_chunk_interest(interest);
                    net_connecting = true;
                    net_connect_elapsed = 0.0;
                    searching_nearby = false;
                    multiplayer_hint = "Joining " + host.name + " (" + host.ip + ")";
                    refresh_multicast_lock_state();
                }
            }
            return;
        }

        NetTickInput input{};
        input.tick = net_tick++;
        input.move_x = touch.left_value.x;
        input.move_y = touch.left_value.y;
        input.camera_yaw_deg = glm::degrees(cam_yaw) + 180.0f;
        input.action_flags = 0;
        if (touch.jump_held) {
            input.action_flags |= net_flag(NetInputFlags::JumpHeld);
        }
        if (touch.jump_pressed) {
            input.action_flags |= net_flag(NetInputFlags::JumpPressed);
        }
        if (touch.sprint_held) {
            input.action_flags |= net_flag(NetInputFlags::SprintHeld);
        }
        if (touch.crouch_held) {
            input.action_flags |= net_flag(NetInputFlags::CrouchHeld);
        }
        net_client.send_input(input);

        NetSnapshot snapshot{};
        if (net_client.poll_snapshot(snapshot)) {
            if (std::isfinite(snapshot.x) && std::isfinite(snapshot.y) && std::isfinite(snapshot.z) &&
                std::fabs(snapshot.x) < 100000.0f && std::fabs(snapshot.y) < 100000.0f && std::fabs(snapshot.z) < 100000.0f) {
                net_target_position = glm::vec3(snapshot.x, snapshot.y, snapshot.z);
                net_reconcile_error = glm::length(player_feet_position - net_target_position);
                net_last_snapshot_tick = snapshot.tick;
                if (!net_target_valid) {
                    player_feet_position = net_target_position;
                }
                net_target_valid = true;
            }
        }

        const uint32_t assigned_id = net_client.local_player_id();
        if (assigned_id != 0) {
            net_local_player_id = assigned_id;
        }

        NetChunkState chunk_state{};
        while (net_client.poll_chunk_state(chunk_state)) {
            if (apply_streamed_world_state(chunk_state)) {
                __android_log_print(
                    ANDROID_LOG_INFO,
                    kLogTag,
                    "Applied streamed chunk state seed=%llu version=%u type=%u",
                    static_cast<unsigned long long>(chunk_state.world_seed),
                    chunk_state.version,
                    static_cast<unsigned>(chunk_state.content_type));
            }
        }

        std::unordered_set<uint32_t> seen_remote_ids;
        for (const auto &[player_id, state] : net_client.player_states()) {
            if (net_local_player_id != 0 && player_id == net_local_player_id) {
                continue;
            }

            glm::vec3 target(state.x, state.y, state.z);
            if (!std::isfinite(target.x) || !std::isfinite(target.y) || !std::isfinite(target.z) ||
                std::fabs(target.x) > 100000.0f || std::fabs(target.y) > 100000.0f || std::fabs(target.z) > 100000.0f) {
                continue;
            }

            RemoteRenderPlayer &remote = remote_render_players[player_id];
            if (!remote.initialized) {
                remote.position = target;
                remote.initialized = true;
            }
            remote.target_position = target;
            remote.velocity = glm::vec3(state.vx, state.vy, state.vz);
            remote.anim_state = state.anim_state;
            remote.anim_phase = state.anim_phase;
            remote.anim_blend = state.anim_blend;
            const glm::vec3 sample_velocity(state.vx, state.vy, state.vz);
            net_remote_push_sample(
                remote,
                state.tick,
                target,
                sample_velocity,
                state.anim_state,
                state.anim_phase,
                state.anim_blend,
                kRemoteSampleHistoryMax);
            seen_remote_ids.insert(player_id);
        }

        for (auto it = remote_render_players.begin(); it != remote_render_players.end();) {
            if (seen_remote_ids.find(it->first) == seen_remote_ids.end()) {
                it = remote_render_players.erase(it);
            } else {
                ++it;
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
        destroy_mesh(remote_players_gpu);
        destroy_mesh(ui_text_gpu);
        if (program != 0) {
            glDeleteProgram(program);
            program = 0;
        }
        u_mvp = -1;
    }

    void rebuild_world_meshes() {
        destroy_mesh(terrain_gpu);
        destroy_mesh(grid_gpu);
        net_generate_chunk_from_state(world, streamed_world_state);
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
        terrain_gpu = upload_mesh(terrain_mesh);
        grid_gpu = upload_mesh(grid_mesh);
    }

    bool apply_streamed_world_state(const NetChunkState &state) {
        if (state.coord.x != 0 || state.coord.z != 0) {
            return false;
        }
        if (net_chunk_state_matches(streamed_world_state, state)) {
            return false;
        }
        streamed_world_state = state;
        if (can_render()) {
            rebuild_world_meshes();
        }
        return true;
    }

    void reset_streamed_world_state() {
        streamed_world_state = net_make_flat_chunk_state(NetChunkCoord{});
        if (can_render()) {
            rebuild_world_meshes();
        }
    }

    void rebuild_player_visual_mesh() {
        destroy_mesh(capsule_gpu);
        capsule_mesh = RenderMesh{};
        const SkinnedModel *selected_player_model = nullptr;
        if (gui_menu.character() == GuiMenu::Character::Humanoid && has_humanoid_player_model) {
            selected_player_model = &humanoid_player_model;
        }
        const bool render_skinned_avatar = selected_player_model != nullptr;
        if (render_skinned_avatar) {
            capsule_mesh = selected_player_model->build_render_mesh(
                player_anim_state,
                player_anim_phase,
                player_anim_state,
                player_anim_phase,
                1.0f,
                glm::vec3(0.0f),
                player_anim_orientation,
                glm::vec3(0.95f, 0.5f, 0.2f));
        } else {
            capsule_mesh = build_debug_capsule_mesh(
                glm::vec3(0.0f),
                player_capsule_radius,
                player_capsule_height,
                glm::vec3(0.95f, 0.5f, 0.2f));
        }

        if (devhud && render_skinned_avatar) {
            RenderMesh debug_capsule = build_debug_capsule_mesh(
                glm::vec3(0.0f),
                player_capsule_radius,
                player_capsule_height,
                glm::vec3(0.95f, 0.6f, 0.3f));
            append_mesh(capsule_mesh, debug_capsule);
        }

        capsule_gpu = upload_mesh(capsule_mesh);
    }

    void rebuild_remote_player_visual_mesh() {
        destroy_mesh(remote_players_gpu);
        remote_players_mesh = RenderMesh{};

        for (const auto &[player_id, remote] : remote_render_players) {
            if (!remote.initialized) {
                continue;
            }

            float radius = player_capsule_radius;
            float height = player_capsule_height;
            float bob = 0.0f;
            switch (remote.anim_state) {
            case static_cast<uint8_t>(PlayerAnimState::StartMove):
            case static_cast<uint8_t>(PlayerAnimState::LocomotionWalk):
            case static_cast<uint8_t>(PlayerAnimState::PivotLeft):
            case static_cast<uint8_t>(PlayerAnimState::PivotRight):
            case static_cast<uint8_t>(PlayerAnimState::TurnInPlaceLeft):
            case static_cast<uint8_t>(PlayerAnimState::TurnInPlaceRight):
            case static_cast<uint8_t>(PlayerAnimState::MovingTurn):
                bob = 0.05f * std::fabs(std::sin(remote.anim_phase));
                break;
            case static_cast<uint8_t>(PlayerAnimState::LocomotionRun):
                bob = 0.09f * std::fabs(std::sin(remote.anim_phase));
                break;
            case static_cast<uint8_t>(PlayerAnimState::StopMove):
            case static_cast<uint8_t>(PlayerAnimState::Recovery):
                height *= 0.55f;
                radius *= 1.08f;
                bob = 0.02f * std::fabs(std::sin(remote.anim_phase * 0.8f));
                break;
            case static_cast<uint8_t>(PlayerAnimState::JumpTakeoff):
            case static_cast<uint8_t>(PlayerAnimState::JumpLoop):
            case static_cast<uint8_t>(PlayerAnimState::FallLoop):
            case static_cast<uint8_t>(PlayerAnimState::LandSoft):
            case static_cast<uint8_t>(PlayerAnimState::LandHard):
                bob = 0.06f * std::sin(remote.anim_phase * 0.65f);
                break;
            default:
                break;
            }

            const glm::vec3 pos = remote.position + glm::vec3(0.0f, bob, 0.0f);
            append_mesh(
                remote_players_mesh,
                build_debug_capsule_mesh(pos, radius, height, player_color_from_id(player_id)));
        }

        if (!remote_players_mesh.vertices.empty()) {
            remote_players_gpu = upload_mesh(remote_players_mesh);
        }
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
        this->app = app;
        multicast_lock.set_activity(app);

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
        invalidate_safe_area();

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

        streamed_world_state = net_make_flat_chunk_state(NetChunkCoord{});
        rebuild_world_meshes();
        player_anim_state = PlayerAnimState::Idle;
        player_anim_phase = 0.0f;
        player_anim_blend = 0.0f;
        player_anim_orientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        has_humanoid_player_model = false;
        {
            auto load_android_model = [&](const char *asset_name, SkinnedModel &out_model, bool &out_loaded, const char *label) {
                std::string load_error;
                std::vector<std::string> model_paths;
                std::string extracted_path;
                const std::string asset_path = std::string("models/player/") + asset_name;
                if (extract_asset_to_file(asset_path.c_str(), extracted_path)) {
                    model_paths.push_back(extracted_path);
                }
                model_paths.push_back(std::string("assets/models/player/") + asset_name);
                model_paths.push_back(std::string("../assets/models/player/") + asset_name);
                model_paths.push_back(std::string("/data/local/tmp/voxov/assets/models/player/") + asset_name);
                for (const std::string &path : model_paths) {
                    if (out_model.load_from_glb(path, load_error)) {
                        out_loaded = true;
                        __android_log_print(ANDROID_LOG_INFO, kLogTag, "Loaded %s model from %s", label, path.c_str());
                        return;
                    }
                }
                __android_log_print(ANDROID_LOG_WARN, kLogTag, "%s model load failed on Android: %s", label, load_error.c_str());
            };
            load_android_model("CesiumMan.glb", humanoid_player_model, has_humanoid_player_model, "humanoid");
        }
        capsule_mesh = RenderMesh{};
        ui_text_mesh = RenderMesh{};
        ui_text_cache.clear();
        terrain_gpu = upload_mesh(terrain_mesh);
        grid_gpu = upload_mesh(grid_mesh);
        rebuild_player_visual_mesh();

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
        multicast_lock.shutdown();
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
        invalidate_safe_area();
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
        if (actions.close_menu && !gameplay_started) {
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
        if (actions.leave_session) {
            leave_session_secure();
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
            const std::string menu_text = gui_menu.build_text(devhud, noclip, session_context());
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
            clear_touch_actions(false);
        }
        const float look_scale = 0.0035f;
        if (gui_menu.open()) {
            clear_touch_actions(true);
        }
        cam_yaw += touch.look_delta.x * look_scale;
        cam_pitch += touch.look_delta.y * look_scale;
        cam_pitch = std::clamp(cam_pitch, -1.2f, 1.2f);
        touch.look_delta = glm::vec2(0.0f);

        const glm::vec3 forward_flat = glm::normalize(glm::vec3(std::sin(cam_yaw), 0.0f, -std::cos(cam_yaw)));
        const glm::vec3 right_flat = glm::normalize(glm::cross(forward_flat, glm::vec3(0.0f, 1.0f, 0.0f)));
        float speed = 6.8f;
        if (touch.crouch_held) {
            speed = 2.2f;
        } else if (touch.sprint_held) {
            speed = 8.8f;
        }
        const glm::vec3 move_delta = (forward_flat * touch.left_value.y + right_flat * touch.left_value.x) * speed * static_cast<float>(dt_seconds);
        const glm::vec3 move_dir = forward_flat * touch.left_value.y + right_flat * touch.left_value.x;
        if (glm::length(glm::vec2(move_dir.x, move_dir.z)) > 0.06f) {
            const float yaw = std::atan2(move_dir.x, move_dir.z);
            player_anim_orientation = glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f));
        }
        if (net_connected && net_target_valid) {
            const float net_err = glm::length(player_feet_position - net_target_position);
            net_reconcile_error = net_err;
            if (std::isfinite(net_err) && net_err > 2.5f) {
                player_feet_position = net_target_position;
                ++net_reconcile_corrections;
            }
            const float follow = std::clamp(static_cast<float>(dt_seconds) * 14.0f, 0.0f, 1.0f);
            player_feet_position = glm::mix(player_feet_position, net_target_position, follow);
        } else if (noclip) {
            player_feet_position += move_delta;
            if (touch.jump_held) {
                player_feet_position.y += speed * static_cast<float>(dt_seconds);
            }
            player_vertical_velocity = 0.0f;
            player_grounded = false;
        } else {
            if (player_grounded && touch.jump_pressed) {
                player_vertical_velocity = 6.3f;
                player_grounded = false;
            }
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

        const bool moving = glm::length(touch.left_value) > 0.12f;
        PlayerAnimState next_state = PlayerAnimState::Idle;
        if (!noclip && !player_grounded) {
            next_state = player_vertical_velocity >= 0.0f ? PlayerAnimState::JumpLoop : PlayerAnimState::FallLoop;
        } else if (moving) {
            next_state = touch.sprint_held ? PlayerAnimState::LocomotionRun : PlayerAnimState::LocomotionWalk;
        }
        player_anim_state = next_state;
        player_anim_phase += player_anim_cycle_rate(player_anim_state) * static_cast<float>(dt_seconds);
        if (player_anim_phase > 6.28318530718f) {
            player_anim_phase = std::fmod(player_anim_phase, 6.28318530718f);
        }
        const float blend_target = player_anim_blend_target(player_anim_state);
        const float blend_lerp = std::clamp(static_cast<float>(dt_seconds) * 10.0f, 0.0f, 1.0f);
        player_anim_blend += (blend_target - player_anim_blend) * blend_lerp;

        net_remote_interpolate(
            remote_render_players,
            remote_interp_tick_cursor,
            remote_interp_tick_cursor_initialized,
            dt_seconds,
            1.0 / 60.0,
            kRemoteInterpDelayTicks);

        touch.jump_pressed = false;
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

        const UiRect menu_button = menu_button_rect();
        draw_rect(menu_button.x - 4, menu_button.y - 4, menu_button.w + 8, menu_button.h + 8, 0.18f, 0.24f, 0.32f);
        draw_rect(menu_button.x, menu_button.y, menu_button.w, menu_button.h, 0.12f, 0.17f, 0.24f);

        if (!gui_menu.open() && gameplay_started) {
            const UiRect jump = jump_button_rect();
            const UiRect sprint = sprint_button_rect();
            const UiRect crouch = crouch_button_rect();

            if (touch.jump_held) {
                draw_rect(jump.x, jump.y, jump.w, jump.h, 0.28f, 0.36f, 0.24f);
                draw_rect(jump.x + 4, jump.y + 4, jump.w - 8, jump.h - 8, 0.18f, 0.30f, 0.14f);
            } else {
                draw_rect(jump.x, jump.y, jump.w, jump.h, 0.16f, 0.22f, 0.15f);
                draw_rect(jump.x + 4, jump.y + 4, jump.w - 8, jump.h - 8, 0.10f, 0.15f, 0.10f);
            }

            if (touch.sprint_held) {
                draw_rect(sprint.x, sprint.y, sprint.w, sprint.h, 0.27f, 0.25f, 0.13f);
                draw_rect(sprint.x + 4, sprint.y + 4, sprint.w - 8, sprint.h - 8, 0.21f, 0.17f, 0.07f);
            } else {
                draw_rect(sprint.x, sprint.y, sprint.w, sprint.h, 0.18f, 0.14f, 0.08f);
                draw_rect(sprint.x + 4, sprint.y + 4, sprint.w - 8, sprint.h - 8, 0.12f, 0.09f, 0.05f);
            }

            if (touch.crouch_held) {
                draw_rect(crouch.x, crouch.y, crouch.w, crouch.h, 0.16f, 0.28f, 0.33f);
                draw_rect(crouch.x + 4, crouch.y + 4, crouch.w - 8, crouch.h - 8, 0.10f, 0.22f, 0.26f);
            } else {
                draw_rect(crouch.x, crouch.y, crouch.w, crouch.h, 0.09f, 0.18f, 0.20f);
                draw_rect(crouch.x + 4, crouch.y + 4, crouch.w - 8, crouch.h - 8, 0.06f, 0.12f, 0.14f);
            }
        }

        if (gui_menu.open()) {
            const GuiMenuView menu_view = gui_menu.build_view(devhud, noclip, session_context());
            const UiMenuLayout layout = menu_layout(menu_view);
            const UiRect panel = layout.panel;
            const int panel_x = panel.x;
            const int panel_y = panel.y;
            const int panel_w = panel.w;
            const int panel_h = panel.h;
            draw_rect(panel_x, panel_y, panel_w, panel_h, 0.06f, 0.08f, 0.12f);
            draw_rect(panel_x + 4, panel_y + 4, panel_w - 8, panel_h - 8, 0.09f, 0.11f, 0.16f);

            const int row_count = static_cast<int>(menu_view.items.size());
            for (int i = 0; i < row_count; ++i) {
                const int row_y = layout.row_start_y + i * (layout.row_h + layout.row_gap);
                const bool selected = (i == gui_menu.selected());
                if (selected) {
                    draw_rect(panel_x + layout.outer_inset - 4, row_y, panel_w - (layout.outer_inset - 4) * 2, layout.row_h, 0.24f, 0.35f, 0.50f);
                    draw_rect(panel_x + layout.inner_inset, row_y + 4, panel_w - layout.inner_inset * 2, layout.row_h - 8, 0.16f, 0.25f, 0.36f);
                } else {
                    draw_rect(panel_x + layout.inner_inset, row_y + 4, panel_w - layout.inner_inset * 2, layout.row_h - 8, 0.11f, 0.16f, 0.24f);
                }
            }
        }

        glDisable(GL_SCISSOR_TEST);

        std::string ui_key;
        ui_key.reserve(256);
        const GuiMenuView menu_view = gui_menu.build_view(devhud, noclip, session_context());
        if (gui_menu.open()) {
            ui_key += "menu:";
            ui_key += std::to_string(static_cast<int>(gui_menu.page_id()));
            ui_key += ":";
            ui_key += std::to_string(menu_view.selected);
            ui_key += ":";
            ui_key += menu_view.status;
            ui_key += ":";
            ui_key += std::to_string(width);
            ui_key += "x";
            ui_key += std::to_string(height);
        } else if (gameplay_started) {
            ui_key = "hud_controls";
            ui_key += std::to_string(width);
            ui_key += "x";
            ui_key += std::to_string(height);
            if (devhud) {
                const NetDebugStats client_stats = net_client.debug_stats();
                const NetDebugStats server_stats = local_server_running ? local_server.debug_stats() : NetDebugStats{};
                ui_key += ":dev:";
                ui_key += std::to_string(net_connected ? 1 : 0);
                ui_key += ":";
                ui_key += std::to_string(static_cast<unsigned>(remote_render_players.size()));
                ui_key += ":";
                ui_key += std::to_string(client_stats.tx_packets_per_sec);
                ui_key += ":";
                ui_key += std::to_string(client_stats.rx_packets_per_sec);
                ui_key += ":";
                ui_key += std::to_string(server_stats.snapshots_sent_per_sec);
                ui_key += ":";
                ui_key += std::to_string(server_stats.player_state_broadcasts_per_sec);
            }
        }

        if (ui_key != ui_text_cache) {
            destroy_mesh(ui_text_gpu);
            ui_text_mesh = RenderMesh{};
            auto text_dims = [](const std::string &text, float px) -> glm::vec2 {
                return glm::vec2(
                    static_cast<float>(text.size()) * 6.0f * px,
                    7.0f * px);
            };
            auto append_text = [&](const std::string &text, float x, float y, float px, const glm::vec3 &color) {
                if (text.empty()) {
                    return;
                }
                RenderMesh part = build_overlay_text_mesh(text, width, height, x, y, px, color, 1.0f);
                append_mesh(ui_text_mesh, part);
            };
            auto append_centered = [&](const UiRect &rect, const std::string &text, float px, const glm::vec3 &color) {
                const glm::vec2 size = text_dims(text, px);
                const float x = static_cast<float>(rect.x) + (static_cast<float>(rect.w) - size.x) * 0.5f;
                const float y = static_cast<float>(rect.y) + (static_cast<float>(rect.h) - size.y) * 0.5f;
                append_text(text, x, y, px, color);
            };

            if (gui_menu.open()) {
                const UiMenuLayout layout = menu_layout(menu_view);
                const UiRect panel = layout.panel;
                const int panel_x = panel.x;
                const int panel_y = panel.y;
                const int panel_w = panel.w;
                const int row_count = static_cast<int>(menu_view.items.size());
                append_text(
                    menu_view.title,
                    static_cast<float>(panel_x + layout.inner_inset),
                    static_cast<float>(layout.title_y),
                    layout.title_px,
                    glm::vec3(0.96f, 0.98f, 1.0f));

                for (size_t i = 0; i < menu_view.guide_lines.size(); ++i) {
                    append_text(
                        menu_view.guide_lines[i],
                        static_cast<float>(panel_x + layout.inner_inset),
                        static_cast<float>(layout.guide_y + static_cast<int>(i) * layout.guide_step),
                        layout.guide_px,
                        glm::vec3(0.92f, 0.95f, 0.99f));
                }

                for (int i = 0; i < row_count; ++i) {
                    const UiRect row_rect{
                        panel_x + layout.inner_inset,
                        layout.row_start_y + i * (layout.row_h + layout.row_gap),
                        panel_w - layout.inner_inset * 2,
                        layout.row_h - 8};
                    std::string label = menu_view.items[static_cast<size_t>(i)];
                    if (label.empty()) {
                        continue;
                    }
                    if (i == menu_view.selected) {
                        label = "> " + label;
                    }
                    append_centered(row_rect, label, layout.item_px, glm::vec3(0.95f, 0.98f, 1.0f));
                }

                if (!menu_view.status.empty()) {
                    append_text(
                        "STATUS: " + menu_view.status,
                        static_cast<float>(panel_x + layout.inner_inset),
                        static_cast<float>(layout.status_y),
                        layout.status_px,
                        glm::vec3(0.85f, 0.9f, 0.98f));
                }
            } else if (gameplay_started) {
                const UiTouchLayout touch_layout_state = touch_layout();
                append_centered(menu_button, "MENU", touch_layout_state.menu_label_px, glm::vec3(0.93f, 0.95f, 0.99f));
                append_centered(crouch_button_rect(), "CRAWL", touch_layout_state.action_label_px, glm::vec3(0.93f, 0.95f, 0.99f));
                append_centered(jump_button_rect(), "JUMP", touch_layout_state.action_label_px, glm::vec3(0.93f, 0.95f, 0.99f));
                append_centered(sprint_button_rect(), "SPRINT", touch_layout_state.action_label_px - 0.1f, glm::vec3(0.93f, 0.95f, 0.99f));
                if (devhud) {
                    const UiRect panel = menu_panel_rect();
                    const NetDebugStats client_stats = net_client.debug_stats();
                    const NetDebugStats server_stats = local_server_running ? local_server.debug_stats() : NetDebugStats{};
                    char line1[160]{};
                    char line2[200]{};
                    char line3[200]{};
                    std::snprintf(
                        line1,
                        sizeof(line1),
                        "NET C%d REM %u LID %u",
                        net_connected ? 1 : 0,
                        static_cast<unsigned>(remote_render_players.size()),
                        net_local_player_id);
                    std::snprintf(
                        line2,
                        sizeof(line2),
                        "CL pps %u/%u Bps %u/%u | SV snap %u pst %u",
                        client_stats.tx_packets_per_sec,
                        client_stats.rx_packets_per_sec,
                        client_stats.tx_bytes_per_sec,
                        client_stats.rx_bytes_per_sec,
                        server_stats.snapshots_sent_per_sec,
                        server_stats.player_state_broadcasts_per_sec);
                    std::snprintf(
                        line3,
                        sizeof(line3),
                        "REC err %.2f tick %u corr %llu",
                        net_reconcile_error,
                        net_last_snapshot_tick,
                        static_cast<unsigned long long>(net_reconcile_corrections));
                    append_text(line1, static_cast<float>(panel.x), static_cast<float>(panel.y), 2.0f, glm::vec3(0.88f, 0.94f, 1.0f));
                    append_text(line2, static_cast<float>(panel.x), static_cast<float>(panel.y + 24), 1.9f, glm::vec3(0.78f, 0.86f, 0.98f));
                    append_text(line3, static_cast<float>(panel.x), static_cast<float>(panel.y + 47), 1.9f, glm::vec3(0.78f, 0.92f, 0.86f));
                }
            }

            if (!ui_text_mesh.vertices.empty()) {
                ui_text_gpu = upload_mesh(ui_text_mesh);
            }
            ui_text_cache = ui_key;
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

        rebuild_player_visual_mesh();
        rebuild_remote_player_visual_mesh();
        draw_mesh(terrain_gpu, mvp);
        draw_mesh(grid_gpu, mvp);
        const glm::mat4 capsule_model = glm::translate(glm::mat4(1.0f), player_feet_position);
        draw_mesh(capsule_gpu, mvp * capsule_model);
        draw_mesh(remote_players_gpu, mvp);
        draw_ui_overlay();

        if (eglSwapBuffers(display, surface) == EGL_FALSE) {
            __android_log_print(ANDROID_LOG_ERROR, kLogTag, "eglSwapBuffers failed: %s", egl_error_to_string(eglGetError()));
            shutdown();
            return;
        }

        ++frame_counter;
        if (frame_counter == 1 || frame_counter % 300 == 0) {
            const NetDebugStats client_stats = net_client.debug_stats();
            const NetDebugStats server_stats = local_server_running ? local_server.debug_stats() : NetDebugStats{};
            __android_log_print(
                ANDROID_LOG_INFO,
                kLogTag,
                "frame=%llu dt=%.3fms player=(%.2f,%.2f,%.2f) cam=(%.2f,%.2f,%.2f) move=(%.2f,%.2f) rem=%u net=%d cpps=%u/%u cbps=%u/%u spps=%u/%u sbps=%u/%u snap=%u pst=%u",
                static_cast<unsigned long long>(frame_counter),
                dt_seconds * 1000.0,
                player_feet_position.x,
                player_feet_position.y,
                player_feet_position.z,
                cam_pos.x,
                cam_pos.y,
                cam_pos.z,
                touch.left_value.x,
                touch.left_value.y,
                static_cast<unsigned>(remote_render_players.size()),
                net_connected ? 1 : 0,
                client_stats.tx_packets_per_sec,
                client_stats.rx_packets_per_sec,
                client_stats.tx_bytes_per_sec,
                client_stats.rx_bytes_per_sec,
                server_stats.tx_packets_per_sec,
                server_stats.rx_packets_per_sec,
                server_stats.tx_bytes_per_sec,
                server_stats.rx_bytes_per_sec,
                server_stats.snapshots_sent_per_sec,
                server_stats.player_state_broadcasts_per_sec);
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
            const UiTouchLayout touch_layout_state = touch_layout();
            if (rect_contains_margin(menu_button_rect(), x, y, static_cast<float>(touch_layout_state.menu_hit_margin))) {
                pending_menu_toggle = true;
                return 1;
            }
            if (!gui_menu.open() && gameplay_started) {
                const UiRect jump = jump_button_rect();
                const UiRect sprint = sprint_button_rect();
                const UiRect crouch = crouch_button_rect();
                const float action_margin = static_cast<float>(touch_layout_state.action_hit_margin);
                if (rect_contains_margin(jump, x, y, action_margin) && touch.jump_pointer == -1) {
                    touch.jump_pointer = pointer_id;
                    touch.jump_held = true;
                    touch.jump_pressed = true;
                    return 1;
                }
                if (rect_contains_margin(sprint, x, y, action_margin) && touch.sprint_pointer == -1) {
                    touch.sprint_pointer = pointer_id;
                    touch.sprint_held = true;
                    return 1;
                }
                if (rect_contains_margin(crouch, x, y, action_margin) && touch.crouch_pointer == -1) {
                    touch.crouch_pointer = pointer_id;
                    touch.crouch_held = true;
                    return 1;
                }
            }
            if (gui_menu.open()) {
                const GuiMenuView menu_view = gui_menu.build_view(devhud, noclip, session_context());
                const UiMenuLayout layout = menu_layout(menu_view);
                const UiRect panel = layout.panel;
                const int panel_x = panel.x;
                const int panel_y = panel.y;
                const int panel_w = panel.w;
                const int panel_h = panel.h;
                const bool inside_panel =
                    (x >= static_cast<float>(panel_x) && x <= static_cast<float>(panel_x + panel_w) &&
                     y >= static_cast<float>(panel_y) && y <= static_cast<float>(panel_y + panel_h));
                if (inside_panel) {
                    const int row_count = static_cast<int>(menu_view.items.size());
                    const float local_y = y - static_cast<float>(layout.row_start_y);
                    if (local_y >= 0.0f) {
                        const float row_span = static_cast<float>(layout.row_h + layout.row_gap);
                        const int tapped_row = static_cast<int>(local_y / row_span);
                        if (tapped_row >= 0 && tapped_row < row_count) {
                            const float in_row_y = local_y - static_cast<float>(tapped_row) * row_span;
                            if (in_row_y <= static_cast<float>(layout.row_h)) {
                                gui_menu.set_selected(tapped_row);
                                pending_menu_select = true;
                                return 1;
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
            if (touch.jump_pointer == pointer_id) {
                touch.jump_pointer = -1;
                touch.jump_held = false;
            }
            if (touch.sprint_pointer == pointer_id) {
                touch.sprint_pointer = -1;
                touch.sprint_held = false;
            }
            if (touch.crouch_pointer == pointer_id) {
                touch.crouch_pointer = -1;
                touch.crouch_held = false;
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
                    if (glm::length(touch.left_value) < 0.12f) {
                        touch.left_value = glm::vec2(0.0f);
                    }
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
