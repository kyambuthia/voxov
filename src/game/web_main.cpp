#include <GLES3/gl3.h>
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

#include <cmath>
#include <cstdio>

namespace {
struct WebAppState {
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context = 0;
    float t = 0.0f;
};

void main_loop(void *arg) {
    auto *state = static_cast<WebAppState *>(arg);
    state->t += 1.0f / 60.0f;

    const float r = 0.12f + 0.08f * std::sin(state->t * 0.9f);
    const float g = 0.18f + 0.08f * std::sin(state->t * 1.4f + 1.0f);
    const float b = 0.24f + 0.08f * std::sin(state->t * 1.1f + 2.0f);

    glViewport(0, 0, 1280, 720);
    glClearColor(r, g, b, 1.0f);
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

    glEnable(GL_DEPTH_TEST);
    emscripten_set_main_loop_arg(main_loop, &state, 0, 1);
    return 0;
}
