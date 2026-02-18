#include "engine_world/voxel_chunk.hpp"
#include "engine_world/physics/voxel_collision.hpp"
#include "engine_render/debug_draw/debug_draw.hpp"

#include <android/input.h>
#include <android/log.h>
#include <android_native_app_glue.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace {
constexpr const char *kLogTag = "VOXOV";

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

    VoxelChunk world{};
    RenderMesh terrain_mesh{};
    RenderMesh grid_mesh{};
    RenderMesh capsule_mesh{};
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

    timespec last_time{};
    bool has_last_time = false;
    uint64_t frame_counter = 0;

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

    void update_player_and_camera(double dt_seconds) {
        const float look_scale = 0.0035f;
        cam_yaw += touch.look_delta.x * look_scale;
        cam_pitch += touch.look_delta.y * look_scale;
        cam_pitch = std::clamp(cam_pitch, -1.2f, 1.2f);
        touch.look_delta = glm::vec2(0.0f);

        const glm::vec3 forward_flat = glm::normalize(glm::vec3(std::sin(cam_yaw), 0.0f, -std::cos(cam_yaw)));
        const glm::vec3 right_flat = glm::normalize(glm::cross(forward_flat, glm::vec3(0.0f, 1.0f, 0.0f)));
        const float speed = 8.0f;
        const glm::vec3 move_delta = (forward_flat * touch.left_value.y + right_flat * touch.left_value.x) * speed * static_cast<float>(dt_seconds);

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

        update_player_and_camera(dt_seconds);

        glViewport(0, 0, width, height);
        glClearColor(0.08f, 0.1f, 0.14f, 1.0f);
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
