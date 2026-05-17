/// Sokol smoke test — minimal triangle + audio beep.
/// Verifies: window opens, triangle renders, audio plays.
/// Build: cmake -DVOXOV_BUILD_TESTS=ON && make voxov_sokol_smoke

#define SOKOL_NO_ENTRY
#define SOKOL_IMPL
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "sokol_log.h"
#include "sokol_audio.h"

#include <cmath>
#include <cstdio>

namespace {

// Shader: flat colour triangle
static const char *kTriangleVsSrc = R"(
    #version 330
    layout(location=0) in vec3 position;
    layout(location=1) in vec3 color0;
    out vec3 v_color;
    void main() {
        v_color = color0;
        gl_Position = vec4(position, 1.0);
    }
)";
static const char *kTriangleFsSrc = R"(
    #version 330
    in vec3 v_color;
    out vec4 frag_color;
    void main() {
        frag_color = vec4(v_color, 1.0);
    }
)";

sg_pipeline g_pipeline{};
sg_buffer g_vbuf{};
sg_pass_action g_pass_action{};

// Audio: simple sine wave beep
float g_audio_phase = 0.0f;
bool g_audio_playing = false;

void audio_callback(float *buffer, int num_frames, int num_channels) {
    if (!g_audio_playing) return;
    for (int i = 0; i < num_frames; ++i) {
        const float sample = 0.3f * std::sin(g_audio_phase * 440.0f * 2.0f * 3.14159265f);
        for (int ch = 0; ch < num_channels; ++ch) {
            buffer[i * num_channels + ch] = sample;
        }
        g_audio_phase += 1.0f / 44100.0f;
    }
}

// Vertex data: RGB triangle
struct Vertex {
    float x, y, z;
    float r, g, b;
};

const Vertex g_vertices[] = {
    {  0.0f,  0.6f, 0.0f, 1.0f, 0.0f, 0.0f }, // top — red
    { -0.6f, -0.4f, 0.0f, 0.0f, 1.0f, 0.0f }, // bottom-left — green
    {  0.6f, -0.4f, 0.0f, 0.0f, 0.0f, 1.0f }, // bottom-right — blue
};

void init() {
    sg_desc sgdesc{};
    sgdesc.environment = sglue_environment();
    sgdesc.logger.func = slog_func;
    sg_setup(&sgdesc);
    if (!sg_isvalid()) {
        std::fprintf(stderr, "sg_setup failed\n");
        sapp_request_quit();
        return;
    }

    // Shader
    sg_shader_desc shd_desc{};
    shd_desc.vertex_func.source = kTriangleVsSrc;
    shd_desc.fragment_func.source = kTriangleFsSrc;
    shd_desc.attrs[0].glsl_name = "position";
    shd_desc.attrs[1].glsl_name = "color0";
    sg_shader shd = sg_make_shader(&shd_desc);
    if (shd.id == SG_INVALID_ID) {
        std::fprintf(stderr, "Shader creation failed\n");
        sapp_request_quit();
        return;
    }

    // Pipeline
    sg_pipeline_desc pipeline_desc{};
    pipeline_desc.shader = shd;
    pipeline_desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
    pipeline_desc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT3;
    pipeline_desc.label = "triangle-pipeline";
    g_pipeline = sg_make_pipeline(&pipeline_desc);

    // Vertex buffer
    sg_buffer_desc vbuf_desc{};
    vbuf_desc.usage.vertex_buffer = true;
    vbuf_desc.data = SG_RANGE(g_vertices);
    vbuf_desc.label = "triangle-vbuf";
    g_vbuf = sg_make_buffer(&vbuf_desc);

    // Clear action
    g_pass_action = {};
    g_pass_action.colors[0].load_action = SG_LOADACTION_CLEAR;
    g_pass_action.colors[0].store_action = SG_STOREACTION_STORE;
    g_pass_action.colors[0].clear_value = { 0.05f, 0.05f, 0.08f, 1.0f };

    // Audio
    saudio_desc audio_desc{};
    audio_desc.stream_cb = audio_callback;
    audio_desc.logger.func = slog_func;
    saudio_setup(&audio_desc);
    g_audio_playing = true;

    std::printf("VOXOV sokol smoke test: triangle + audio beep\n");
}

void frame() {
    sg_pass pass{};
    pass.action = g_pass_action;
    pass.swapchain = sglue_swapchain();
    sg_begin_pass(&pass);

    sg_apply_pipeline(g_pipeline);
    sg_bindings bindings{};
    bindings.vertex_buffers[0] = g_vbuf;
    sg_apply_bindings(&bindings);
    sg_draw(0, 3, 1);

    sg_end_pass();
    sg_commit();
}

void cleanup() {
    g_audio_playing = false;
    saudio_shutdown();
    if (g_vbuf.id) sg_destroy_buffer(g_vbuf);
    if (g_pipeline.id) sg_destroy_pipeline(g_pipeline);
    sg_shutdown();
    std::printf("VOXOV sokol smoke test: shutdown\n");
}

} // namespace

int main() {
    sapp_desc desc{};
    desc.init_cb = init;
    desc.frame_cb = frame;
    desc.cleanup_cb = cleanup;
    desc.width = 640;
    desc.height = 480;
    desc.window_title = "Voxov Sokol Smoke Test";
#if defined(SOKOL_GLCORE) && defined(__linux__)
    desc.gl.major_version = 3;
    desc.gl.minor_version = 3;
#endif
    desc.logger.func = slog_func;

    sapp_run(&desc);
    return 0;
}
