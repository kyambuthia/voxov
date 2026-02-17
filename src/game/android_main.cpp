#include <android/log.h>
#include <android_native_app_glue.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include <cmath>
#include <cstdint>
#include <ctime>

namespace {
constexpr const char *kLogTag = "VOXOV";

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

struct AndroidRenderer {
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
    int32_t width = 0;
    int32_t height = 0;
    bool focused = false;
    int gles_version = 0;
    uint64_t frame_counter = 0;

    bool can_render() const {
        return display != EGL_NO_DISPLAY && surface != EGL_NO_SURFACE && context != EGL_NO_CONTEXT;
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
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,
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

        __android_log_print(
            ANDROID_LOG_INFO, kLogTag, "EGL ready (%d x %d), GLES%d: %s",
            width, height, gles_version, glGetString(GL_VERSION));
        return true;
    }

    void shutdown() {
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
        frame_counter = 0;
    }

    void render_frame() {
        if (!can_render()) {
            return;
        }

        timespec ts{};
        clock_gettime(CLOCK_MONOTONIC, &ts);
        const float t = static_cast<float>(ts.tv_sec) + static_cast<float>(ts.tv_nsec) * 1.0e-9f;

        const float pulse = 0.5f + 0.5f * std::sin(t * 0.9f);
        const float r = 0.08f + 0.22f * pulse;
        const float g = 0.10f + 0.18f * (1.0f - pulse);
        const float b = 0.22f + 0.35f * pulse;

        glViewport(0, 0, width, height);
        glClearColor(r, g, b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (eglSwapBuffers(display, surface) == EGL_FALSE) {
            __android_log_print(ANDROID_LOG_ERROR, kLogTag, "eglSwapBuffers failed: %s", egl_error_to_string(eglGetError()));
            shutdown();
            return;
        }

        ++frame_counter;
        if (frame_counter == 1 || frame_counter % 300 == 0) {
            __android_log_print(ANDROID_LOG_INFO, kLogTag, "frame=%llu size=%dx%d focused=%d", frame_counter, width, height, focused ? 1 : 0);
        }
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
}

void android_main(android_app *app) {
    app_dummy();

    AndroidRenderer renderer{};
    app->userData = &renderer;
    app->onAppCmd = handle_app_cmd;
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
        }

        if (!renderer.can_render() && app->window != nullptr) {
            renderer.initialize(app);
        }

        if (renderer.can_render()) {
            renderer.render_frame();
        }
    }
}
