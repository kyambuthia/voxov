#include "engine_core/timing.hpp"
#include "game/android_runtime_adapter.hpp"
#include "game/game_runtime.hpp"
#include "platform/platform_services.hpp"

#include <android/asset_manager.h>
#include <android/configuration.h>
#include <android/input.h>
#include <android/log.h>
#include <android_native_app_glue.h>
#include <EGL/egl.h>
#include <jni.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace {
constexpr const char *kLogTag = "VOXOV";

#ifndef EGL_OPENGL_ES3_BIT
#ifdef EGL_OPENGL_ES3_BIT_KHR
#define EGL_OPENGL_ES3_BIT EGL_OPENGL_ES3_BIT_KHR
#else
#define EGL_OPENGL_ES3_BIT 0x0040
#endif
#endif

struct AndroidSafeArea {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
};

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

JNIEnv *attach_env(android_app *app, bool &attached) {
    attached = false;
    if (!app || !app->activity || !app->activity->vm) {
        return nullptr;
    }

    JNIEnv *env = nullptr;
    if (app->activity->vm->GetEnv(
            reinterpret_cast<void **>(&env),
            JNI_VERSION_1_6) == JNI_OK) {
        return env;
    }

    if (app->activity->vm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "AttachCurrentThread failed");
        return nullptr;
    }
    attached = true;
    return env;
}

void detach_env(android_app *app, bool attached) {
    if (attached && app && app->activity && app->activity->vm) {
        app->activity->vm->DetachCurrentThread();
    }
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
        JNIEnv *env = attach_env(app, attached);
        if (!env || !ensure_lock(env)) {
            detach_env(app, attached);
            return;
        }
        if (call_lock_method(env, "acquire", "()V")) {
            held = true;
            __android_log_print(
                ANDROID_LOG_INFO,
                kLogTag,
                "Android multicast lock acquired");
        }
        detach_env(app, attached);
    }

    void release() {
        if (!held && lock_global == nullptr) {
            return;
        }
        bool attached = false;
        JNIEnv *env = attach_env(app, attached);
        if (!env) {
            return;
        }
        if (lock_global != nullptr && held) {
            call_lock_method(env, "release", "()V");
        }
        held = false;
        detach_env(app, attached);
    }

    void shutdown() {
        release();

        bool attached = false;
        JNIEnv *env = attach_env(app, attached);
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
        detach_env(app, attached);
    }

private:
    bool ensure_lock(JNIEnv *env) {
        if (lock_global != nullptr) {
            return true;
        }
        if (!env || !app || !app->activity || app->activity->clazz == nullptr) {
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
        jobject wifi_manager_local =
            env->CallObjectMethod(activity, get_system_service, wifi_service);
        env->DeleteLocalRef(wifi_service);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            return false;
        }
        if (!wifi_manager_local) {
            return false;
        }

        jclass wifi_cls = env->GetObjectClass(wifi_manager_local);
        jmethodID create_lock = wifi_cls
            ? env->GetMethodID(
                  wifi_cls,
                  "createMulticastLock",
                  "(Ljava/lang/String;)Landroid/net/wifi/WifiManager$MulticastLock;")
            : nullptr;
        if (wifi_cls) {
            env->DeleteLocalRef(wifi_cls);
        }
        if (!create_lock) {
            env->DeleteLocalRef(wifi_manager_local);
            return false;
        }

        jstring lock_name = env->NewStringUTF("voxov_lan_discovery");
        jobject lock_local =
            env->CallObjectMethod(wifi_manager_local, create_lock, lock_name);
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
            jmethodID set_ref_counted =
                env->GetMethodID(lock_cls, "setReferenceCounted", "(Z)V");
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
        if (!env || !lock_global) {
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

AndroidSafeArea fallback_safe_area(const RenderSurface &surface) {
    const int shortest_edge = std::max(1, std::min(surface.width, surface.height));
    return AndroidSafeArea{
        .left = std::max(18, shortest_edge / 28),
        .top = std::max(26, shortest_edge / 18),
        .right = std::max(18, shortest_edge / 28),
        .bottom = std::max(20, shortest_edge / 24),
    };
}

AndroidSafeArea query_android_safe_area(android_app *app, const RenderSurface &surface) {
    AndroidSafeArea safe = fallback_safe_area(surface);
    bool attached = false;
    JNIEnv *env = attach_env(app, attached);
    if (!env || !app || !app->activity || app->activity->clazz == nullptr) {
        detach_env(app, attached);
        return safe;
    }

    jobject activity = app->activity->clazz;
    jclass activity_cls = env->GetObjectClass(activity);
    const jmethodID get_window = activity_cls
        ? env->GetMethodID(activity_cls, "getWindow", "()Landroid/view/Window;")
        : nullptr;
    if (activity_cls) {
        env->DeleteLocalRef(activity_cls);
    }
    if (!get_window) {
        detach_env(app, attached);
        return safe;
    }

    jobject window = env->CallObjectMethod(activity, get_window);
    if (env->ExceptionCheck() || !window) {
        env->ExceptionClear();
        detach_env(app, attached);
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
        detach_env(app, attached);
        return safe;
    }

    jobject decor_view = env->CallObjectMethod(window, get_decor_view);
    env->DeleteLocalRef(window);
    if (env->ExceptionCheck() || !decor_view) {
        env->ExceptionClear();
        detach_env(app, attached);
        return safe;
    }

    jclass view_cls = env->GetObjectClass(decor_view);
    const jmethodID get_root_window_insets = view_cls
        ? env->GetMethodID(
              view_cls,
              "getRootWindowInsets",
              "()Landroid/view/WindowInsets;")
        : nullptr;
    if (view_cls) {
        env->DeleteLocalRef(view_cls);
    }
    if (!get_root_window_insets) {
        env->DeleteLocalRef(decor_view);
        detach_env(app, attached);
        return safe;
    }

    jobject insets = env->CallObjectMethod(decor_view, get_root_window_insets);
    env->DeleteLocalRef(decor_view);
    if (env->ExceptionCheck() || !insets) {
        env->ExceptionClear();
        detach_env(app, attached);
        return safe;
    }

    jclass insets_cls = env->GetObjectClass(insets);
    if (insets_cls) {
        const jmethodID left =
            env->GetMethodID(insets_cls, "getSystemWindowInsetLeft", "()I");
        const jmethodID top =
            env->GetMethodID(insets_cls, "getSystemWindowInsetTop", "()I");
        const jmethodID right =
            env->GetMethodID(insets_cls, "getSystemWindowInsetRight", "()I");
        const jmethodID bottom =
            env->GetMethodID(insets_cls, "getSystemWindowInsetBottom", "()I");
        if (left && top && right && bottom) {
            safe.left = std::max(safe.left, static_cast<int>(env->CallIntMethod(insets, left)) + 8);
            safe.top = std::max(safe.top, static_cast<int>(env->CallIntMethod(insets, top)) + 8);
            safe.right = std::max(safe.right, static_cast<int>(env->CallIntMethod(insets, right)) + 8);
            safe.bottom = std::max(safe.bottom, static_cast<int>(env->CallIntMethod(insets, bottom)) + 8);
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
            }
        }
        env->DeleteLocalRef(insets_cls);
    }
    env->DeleteLocalRef(insets);
    detach_env(app, attached);
    return safe;
}

bool extract_asset_to_file(
    android_app *app,
    const PlatformServices &platform_services,
    const char *asset_path,
    std::string &out_path) {
    if (!app || !app->activity || !app->activity->assetManager ||
        !app->activity->internalDataPath || !asset_path) {
        return false;
    }

    AAsset *asset = AAssetManager_open(
        app->activity->assetManager,
        asset_path,
        AASSET_MODE_STREAMING);
    if (!asset) {
        return false;
    }

    const off_t len = AAsset_getLength(asset);
    if (len <= 0) {
        AAsset_close(asset);
        return false;
    }

    std::vector<uint8_t> data(static_cast<size_t>(len));
    const int read_bytes = AAsset_read(asset, data.data(), data.size());
    AAsset_close(asset);
    if (read_bytes != len) {
        return false;
    }

    const std::filesystem::path path = platform_services.temp_asset_path(asset_path);
    if (!platform_services.write_binary_file(path, data.data(), data.size())) {
        return false;
    }
    out_path = path.generic_string();
    return true;
}

class AndroidHost {
public:
    explicit AndroidHost(android_app *app_state)
        : app(app_state),
          input_adapter(surface_state),
          platform_adapter(surface_state) {
        platform_services = PlatformServices::android(
            app && app->activity ? app->activity->internalDataPath : nullptr);
        multicast_lock.set_activity(app);
    }

    bool initialize() {
        if (!app || !app->window || can_render()) {
            return false;
        }

        display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (display == EGL_NO_DISPLAY) {
            log_egl_error("eglGetDisplay");
            return false;
        }

        EGLint major = 0;
        EGLint minor = 0;
        if (eglInitialize(display, &major, &minor) == EGL_FALSE) {
            log_egl_error("eglInitialize");
            shutdown();
            return false;
        }

        const EGLint config_attrs[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RED_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8,
            EGL_ALPHA_SIZE, 8,
            EGL_DEPTH_SIZE, 24,
            EGL_NONE,
        };

        EGLConfig config = nullptr;
        EGLint num_configs = 0;
        if (eglChooseConfig(
                display,
                config_attrs,
                &config,
                1,
                &num_configs) == EGL_FALSE ||
            num_configs < 1) {
            log_egl_error("eglChooseConfig");
            shutdown();
            return false;
        }

        surface = eglCreateWindowSurface(display, config, app->window, nullptr);
        if (surface == EGL_NO_SURFACE) {
            log_egl_error("eglCreateWindowSurface");
            shutdown();
            return false;
        }

        const EGLint context_attrs[] = {
            EGL_CONTEXT_CLIENT_VERSION, 3,
            EGL_NONE,
        };
        context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attrs);
        if (context == EGL_NO_CONTEXT) {
            log_egl_error("eglCreateContext");
            shutdown();
            return false;
        }

        if (eglMakeCurrent(display, surface, surface, context) == EGL_FALSE) {
            log_egl_error("eglMakeCurrent");
            shutdown();
            return false;
        }

        eglSwapInterval(display, 1);
        update_surface();
        safe_area = query_android_safe_area(app, surface_state.surface);
        (void)safe_area;

        GameRuntimeInitParams params{};
        params.platform = RuntimePlatform::Android;
        params.options.render_backend = RenderBackendType::Sokol;
        params.input_adapter = &input_adapter;
        params.platform_adapter = &platform_adapter;
        params.platform_services = &platform_services;

        if (!runtime.init(params)) {
            __android_log_print(ANDROID_LOG_ERROR, kLogTag, "GameRuntime::init failed");
            shutdown();
            return false;
        }

        frame_pacer.init(120.0);
        runtime_ready = true;
        __android_log_print(
            ANDROID_LOG_INFO,
            kLogTag,
            "EGL + GameRuntime ready (%d x %d)",
            surface_state.surface.width,
            surface_state.surface.height);
        return true;
    }

    void shutdown() {
        if (runtime_ready) {
            runtime.shutdown();
            runtime_ready = false;
        }
        multicast_lock.shutdown();

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
        surface_state.surface = RenderSurface{};
    }

    bool can_render() const {
        return runtime_ready &&
               display != EGL_NO_DISPLAY &&
               surface != EGL_NO_SURFACE &&
               context != EGL_NO_CONTEXT;
    }

    void render_frame() {
        if (!can_render()) {
            return;
        }

        update_surface();
        frame_pacer.begin_frame();
        runtime.tick(frame_pacer.frame_dt());
        if (eglSwapBuffers(display, surface) == EGL_FALSE) {
            log_egl_error("eglSwapBuffers");
        }
        input_adapter.reset_frame_delta();
        frame_pacer.end_frame();
    }

    int32_t on_input(AInputEvent *event) {
        return input_adapter.handle_input(event);
    }

    void handle_command(int32_t cmd) {
        switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            __android_log_print(ANDROID_LOG_INFO, kLogTag, "APP_CMD_INIT_WINDOW");
            initialize();
            break;
        case APP_CMD_TERM_WINDOW:
            __android_log_print(ANDROID_LOG_INFO, kLogTag, "APP_CMD_TERM_WINDOW");
            shutdown();
            break;
        case APP_CMD_GAINED_FOCUS:
            __android_log_print(ANDROID_LOG_INFO, kLogTag, "APP_CMD_GAINED_FOCUS");
            surface_state.focused = true;
            multicast_lock.acquire();
            break;
        case APP_CMD_LOST_FOCUS:
            __android_log_print(ANDROID_LOG_INFO, kLogTag, "APP_CMD_LOST_FOCUS");
            surface_state.focused = false;
            multicast_lock.release();
            break;
        case APP_CMD_CONFIG_CHANGED:
            update_surface();
            safe_area = query_android_safe_area(app, surface_state.surface);
            break;
        default:
            break;
        }
    }

    void request_close() {
        surface_state.close_requested = true;
    }

private:
    void update_surface() {
        if (display == EGL_NO_DISPLAY || surface == EGL_NO_SURFACE) {
            return;
        }
        EGLint width = 1;
        EGLint height = 1;
        eglQuerySurface(display, surface, EGL_WIDTH, &width);
        eglQuerySurface(display, surface, EGL_HEIGHT, &height);

        float dpi_scale = 1.0f;
        if (app && app->config) {
            const int density = AConfiguration_getDensity(app->config);
            if (density > 0 &&
                density != ACONFIGURATION_DENSITY_DEFAULT &&
                density != ACONFIGURATION_DENSITY_ANY &&
                density != ACONFIGURATION_DENSITY_NONE) {
                dpi_scale = static_cast<float>(density) / 160.0f;
            }
        }
        surface_state.surface = RenderSurface{
            .width = std::max(1, static_cast<int>(width)),
            .height = std::max(1, static_cast<int>(height)),
            .dpi_scale = dpi_scale,
        };
    }

    void log_egl_error(const char *operation) const {
        __android_log_print(
            ANDROID_LOG_ERROR,
            kLogTag,
            "%s failed: %s",
            operation,
            egl_error_to_string(eglGetError()));
    }

    android_app *app = nullptr;
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
    PlatformServices platform_services{};
    AndroidRuntimeSurface surface_state{};
    AndroidRuntimeInputAdapter input_adapter;
    AndroidRuntimePlatformAdapter platform_adapter;
    GameRuntime runtime{};
    FramePacer frame_pacer{};
    AndroidMulticastLock multicast_lock{};
    AndroidSafeArea safe_area{};
    bool runtime_ready = false;
};

void handle_app_cmd(android_app *app, int32_t cmd) {
    auto *host = reinterpret_cast<AndroidHost *>(app->userData);
    if (host != nullptr) {
        host->handle_command(cmd);
    }
}

int32_t handle_input(android_app *app, AInputEvent *event) {
    auto *host = reinterpret_cast<AndroidHost *>(app->userData);
    return host != nullptr ? host->on_input(event) : 0;
}
} // namespace

void android_main(android_app *app) {
    app_dummy();

    AndroidHost host(app);
    app->userData = &host;
    app->onAppCmd = handle_app_cmd;
    app->onInputEvent = handle_input;
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "android_main started");

    while (true) {
        int events = 0;
        android_poll_source *source = nullptr;
        const int timeout_ms = host.can_render() ? 0 : -1;
        while (ALooper_pollOnce(
                   timeout_ms,
                   nullptr,
                   &events,
                   reinterpret_cast<void **>(&source)) >= 0) {
            if (source) {
                source->process(app, source);
            }

            if (app->destroyRequested != 0) {
                host.request_close();
                host.shutdown();
                __android_log_print(ANDROID_LOG_INFO, kLogTag, "android_main exit");
                return;
            }

            if (host.can_render()) {
                break;
            }
        }

        if (!host.can_render() && app->window != nullptr) {
            host.initialize();
        }

        if (host.can_render()) {
            host.render_frame();
        }
    }
}
