#include <android/log.h>
#include <android_native_app_glue.h>

namespace {
constexpr const char *kLogTag = "VOXOV";

void handle_app_cmd(android_app *app, int32_t cmd) {
    (void)app;
    switch (cmd) {
    case APP_CMD_INIT_WINDOW:
        __android_log_print(ANDROID_LOG_INFO, kLogTag, "APP_CMD_INIT_WINDOW");
        break;
    case APP_CMD_TERM_WINDOW:
        __android_log_print(ANDROID_LOG_INFO, kLogTag, "APP_CMD_TERM_WINDOW");
        break;
    case APP_CMD_GAINED_FOCUS:
        __android_log_print(ANDROID_LOG_INFO, kLogTag, "APP_CMD_GAINED_FOCUS");
        break;
    case APP_CMD_LOST_FOCUS:
        __android_log_print(ANDROID_LOG_INFO, kLogTag, "APP_CMD_LOST_FOCUS");
        break;
    default:
        break;
    }
}
}

void android_main(android_app *app) {
    app_dummy();
    app->onAppCmd = handle_app_cmd;
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "android_main started");

    while (true) {
        int events = 0;
        android_poll_source *source = nullptr;
        while (ALooper_pollOnce(0, nullptr, &events, reinterpret_cast<void **>(&source)) >= 0) {
            if (source) {
                source->process(app, source);
            }

            if (app->destroyRequested != 0) {
                __android_log_print(ANDROID_LOG_INFO, kLogTag, "android_main exit");
                return;
            }
        }
    }
}
