# Voxov GLFW/OpenGL to sokol Technical Migration Roadmap

This document replaces the compressed Phase 1 in `docs/roadmap.md` with PR-sized engineering work. It uses the current codebase inventory and the GEA 1.6 runtime stack as the dependency rule:

`Third-Party SDKs -> Platform Independence -> Core Systems -> Resource Manager -> Rendering -> Debug/Profiling -> Physics -> Animation -> HID -> Audio -> Networking -> Gameplay Foundations -> Game-Specific`.

Primary sokol references used:

- `sokol_app.h`: https://raw.githubusercontent.com/floooh/sokol/master/sokol_app.h
- `sokol_gfx.h`: https://raw.githubusercontent.com/floooh/sokol/master/sokol_gfx.h
- `sokol_glue.h`: https://github.com/floooh/sokol/blob/master/util/sokol_glue.h
- `sokol_imgui.h`: https://raw.githubusercontent.com/floooh/sokol/master/util/sokol_imgui.h
- `sokol-shdc`: https://raw.githubusercontent.com/floooh/sokol-tools/master/docs/sokol-shdc.md
- samples: https://github.com/floooh/sokol-samples

## Migration Inventory

Generated with:

```bash
rg -n "\bglfw[A-Z][A-Za-z0-9_]*\s*\(|GLFW_[A-Z0-9_]+|GLFWwindow" src CMakeLists.txt
rg -n "\bgl[A-Z][A-Za-z0-9_]*\s*\(" src/engine_render src/game src/platform src/renderer src/world --glob '!src/third_party/**'
rg -n "imgui_impl|ImGui_Impl|<imgui_impl|sokol_imgui|miniaudio|MINIAUDIO|ma_" src CMakeLists.txt dependencies/miniaudio -g '!dependencies/miniaudio/miniaudio.h'
```

### GLFW Call Sites

| File | Lines | Current dependency | Replacement |
|---|---:|---|---|
| `CMakeLists.txt` | 148-150 | GLFW build options | move under `VOXOV_RENDER_BACKEND_GL` |
| `CMakeLists.txt` | 156 | `add_subdirectory(dependencies/glfw)` | move under GL platform target |
| `CMakeLists.txt` | 178 | `imgui` links `glfw` | replace with `sokol_imgui` target for sokol path |
| `CMakeLists.txt` | 194 | `platform_desktop PUBLIC glfw` | split `platform_desktop_glfw` and `platform_sokol` |
| `CMakeLists.txt` | 228 | `engine_render PUBLIC OpenGL::GL glfw` | make backend deps private to backend libs |
| `CMakeLists.txt` | 366 | `game_runtime PUBLIC engine platform_desktop` | depend on platform interface only |
| `CMakeLists.txt` | 390 | `voxov` links `glfw` | only `voxov_glfw` links this |
| `src/platform/platform.hpp` | 5, 27, 37 | public `GLFWwindow` leak | remove from public platform API |
| `src/platform/platform.cpp` | 6, 10-17, 19, 25, 32-33, 40, 43, 47, 51, 58, 63, 68, 82, 91-94, 98, 105 | window/context/event/fullscreen/time/title APIs | sokol owns app entry and events; no `poll_events()` for sokol |
| `src/platform/desktop/input_desktop.hpp` | 5, 9, 15 | `GLFWwindow*` in input backend | input consumes `PlatformInputSnapshot` |
| `src/platform/desktop/input_desktop.cpp` | 6, 15-17, 29-30, 48-51, 70, 75, 83, 87, 91, 95-96, 101, 105, 109, 113, 117, 121-124, 128, 145, 148 | direct polling input | event-fed HID state |
| `src/game/desktop_runtime_adapter.cpp` | 7, 13, 16, 19, 22, 30, 33, 36, 39, 44-45, 49-52, 62, 69 | split-screen direct polling | same HID state with `PlayerSlot` mappings |
| `src/game/main.cpp` | 8, 206 | direct GLFW include and F11 polling | handle fullscreen action via platform event/input action |
| `src/engine_render/gl_renderer.hpp` | 10, 44 | `GLFWwindow*` renderer member | GL backend only |
| `src/engine_render/gl_renderer.cpp` | 3, 140, 302-303, 587, 743 | GL proc load, context, framebuffer size, swap | GL backend only |

### OpenGL Call Sites

| File | Lines | Current usage | sokol mapping |
|---|---:|---|---|
| `src/engine_render/gl_renderer.cpp` | 82-84, 96, 103 | compile shader | generated `sg_shader_desc`, `sg_make_shader` |
| `src/engine_render/gl_renderer.cpp` | 218, 225, 234-242, 257, 263, 272, 278, 292 | link program/uniform lookup | `sg_shader`, `sg_pipeline`, uniform block constants |
| `src/engine_render/gl_renderer.cpp` | 304-307 | global depth/cull state | `sg_pipeline_desc.depth`, `.cull_mode`, `.face_winding` |
| `src/engine_render/gl_renderer.cpp` | 423, 431, 439, 457, 464, 471 | buffer/VAO lifetime | `sg_buffer` lifetime, no VAO |
| `src/engine_render/gl_renderer.cpp` | 496-519 | upload and vertex attrib layout | `sg_make_buffer`/`sg_update_buffer`, `sg_pipeline_desc.layout.attrs` |
| `src/engine_render/gl_renderer.cpp` | 561, 568, 573, 577 | uniform, bind, draw | `sg_apply_uniforms`, `sg_apply_bindings`, `sg_draw` |
| `src/engine_render/gl_renderer.cpp` | 592-594, 601, 619, 643, 648, 654, 659, 668, 673, 680 | viewport, clear, mutable state | render pass action, pipeline variants, `sg_apply_viewport` if needed |
| `src/game/android_main.cpp` | 104-181, 1495, 1499, 1512, 1649-1686, 1790-1804, 1861, 2068-2082, 2103-2109, 2172, 2353, 2377-2383 | standalone EGL/GLES renderer | [NEEDS INVESTIGATION: decide if Android path is replaced by sokol_app or kept as legacy sample] |
| `src/game/web_main.cpp` | 102-230, 426-432, 626 | standalone WebGL2 renderer | [NEEDS INVESTIGATION: decide if Web path is replaced by sokol_app HTML5 or kept as web demo] |
| `src/third_party/stb_truetype.h` | 292-318 | sample code inside third-party header | ignore unless copied into runtime code |

### ImGui Backend Dependencies

| File | Lines | Current dependency | Replacement |
|---|---:|---|---|
| `CMakeLists.txt` | 171-172 | `imgui_impl_glfw.cpp`, `imgui_impl_opengl3.cpp` | `sokol_imgui.h` implementation TU |
| `CMakeLists.txt` | 178 | `imgui PUBLIC glfw OpenGL::GL` | no public GL/GLFW dependency |
| `src/engine_render/gl_renderer.cpp` | 20-21 | backend headers | GL backend only |
| `src/engine_render/gl_renderer.cpp` | 322-323 | `ImGui_ImplGlfw_InitForOpenGL`, `ImGui_ImplOpenGL3_Init` | `simgui_setup` |
| `src/engine_render/gl_renderer.cpp` | 330-331 | backend shutdown | `simgui_shutdown` |
| `src/engine_render/gl_renderer.cpp` | 687-688 | backend new frame | `simgui_new_frame` |
| `src/engine_render/gl_renderer.cpp` | 739 | backend draw | `simgui_render` inside active sokol pass |

### miniaudio Call Sites

| File | Lines | Current dependency | Decision |
|---|---:|---|---|
| `CMakeLists.txt` | 77, 268, 335 | miniaudio include leakage to Android/engine | make private to `engine_audio_miniaudio` |
| `src/engine_audio/ui_audio.cpp` | 5-6 | implementation include | keep for Phase 1; do not replace with `sokol_audio` yet |
| `src/engine_audio/ui_audio.cpp` | 11-15 | `ma_engine`, decoders, sounds | hidden behind `IAudioBackend` |
| `src/engine_audio/ui_audio.cpp` | 31, 38-50, 68-78, 91-100 | init/decode/play/shutdown | backend implementation detail |

### Public CMake Target Leakage

| Target | Lines | Leaks |
|---|---:|---|
| `imgui` | 165-178 | public GLFW/OpenGL |
| `platform_desktop` | 189-194 | public GLFW |
| `engine_render` | 221-228 | public GLFW/OpenGL/ImGui |
| `engine` | 331-352 | inherits miniaudio, cgltf, render/platform backend deps |
| `game_runtime` | 360-366 | inherits desktop platform |
| `voxov` | 384-392 | direct GLFW/OpenGL |
| `voxov_android` | 45-98 | EGL/GLES and miniaudio include path |
| `voxov_web` | 110-126 | WebGL2 flags and raw GLES calls |

## Render Backend Contract

Current contract:

```cpp
class IRenderBackend {
public:
    virtual void init(void *window_handle) = 0;
    virtual void shutdown() = 0;
    virtual void upload_scene(const RenderScene &scene) = 0;
    virtual void update_dynamic_meshes(const RenderMesh &debug_world,
                                       const RenderMesh &debug_screen) = 0;
    virtual void begin_frame(const RenderFrameContext &ctx,
                             const RenderStats &stats) = 0;
    virtual void end_frame() = 0;
};
```

Replace it with a backend contract that has no native window pointer and makes swapchain ownership explicit:

```cpp
enum class RenderBackendType : uint8_t {
    OpenGL,
    Sokol,
};

struct RenderDeviceDesc {
    RenderBackendType backend = RenderBackendType::Sokol;
    int color_format = 0;        // sg_pixel_format value for sokol path.
    int depth_format = 0;        // sg_pixel_format value for sokol path.
    int sample_count = 1;
    bool enable_imgui = true;
};

struct RenderSurface {
    int width = 1;
    int height = 1;
    float dpi_scale = 1.0f;
};

class IRenderBackend {
public:
    virtual ~IRenderBackend() = default;
    virtual bool init(const RenderDeviceDesc &desc) = 0;
    virtual void shutdown() = 0;
    virtual void upload_scene(const RenderScene &scene) = 0;
    virtual void update_dynamic_meshes(const RenderMesh &debug_world,
                                       const RenderMesh &debug_screen) = 0;
    virtual void render_frame(const RenderFrameContext &ctx,
                              const RenderStats &stats,
                              const RenderSurface &surface) = 0;
};
```

### sokol Resource Types

Use these exact low-level resources in `src/engine_render/sokol_renderer.hpp`:

```cpp
#include "sokol_gfx.h"

struct SokolRenderVertex {
    float px, py, pz;
    float cr, cg, cb;
    float nx, ny, nz;
};

struct SokolGpuMesh {
    sg_buffer vertex_buffer{};
    sg_buffer index_buffer{};
    uint32_t index_count = 0;
    glm::vec3 bounds_min{};
    glm::vec3 bounds_max{};
    uint8_t material = 0;
};

struct SokolPipelines {
    sg_shader scene_shader{};
    sg_pipeline opaque{};
    sg_pipeline debug_no_cull{};
    sg_pipeline debug_xray{};
    sg_pipeline screen{};
};

class SokolRenderer final : public IRenderBackend {
public:
    bool init(const RenderDeviceDesc &desc) override;
    void shutdown() override;
    void upload_scene(const RenderScene &scene) override;
    void update_dynamic_meshes(const RenderMesh &debug_world,
                               const RenderMesh &debug_screen) override;
    void render_frame(const RenderFrameContext &ctx,
                      const RenderStats &stats,
                      const RenderSurface &surface) override;

private:
    void upload_mesh(SokolGpuMesh &dst, const RenderMesh &src, bool stream);
    void destroy_mesh(SokolGpuMesh &mesh);
    void draw_mesh(const SokolGpuMesh &mesh, const glm::mat4 &mvp);

    SokolPipelines pipelines_{};
    SokolGpuMesh transient_mesh_{};
    SokolGpuMesh debug_grid_mesh_{};
    SokolGpuMesh debug_world_mesh_{};
    SokolGpuMesh debug_screen_mesh_{};
    std::unordered_map<uint64_t, SokolGpuMesh> cached_meshes_;
};
```

### Resource Creation Pattern

Immutable/static mesh:

```cpp
dst.vertex_buffer = sg_make_buffer(&(sg_buffer_desc){
    .usage = { .vertex_buffer = true },
    .data = SG_RANGE(vertices),
    .label = "voxov-static-vbuf",
});
dst.index_buffer = sg_make_buffer(&(sg_buffer_desc){
    .usage = { .index_buffer = true },
    .data = SG_RANGE(src.indices),
    .label = "voxov-static-ibuf",
});
```

Dynamic mesh:

```cpp
dst.vertex_buffer = sg_make_buffer(&(sg_buffer_desc){
    .usage = { .vertex_buffer = true, .stream_update = true },
    .size = max_vertices * sizeof(SokolRenderVertex),
    .label = "voxov-dynamic-vbuf",
});

sg_update_buffer(dst.vertex_buffer, &(sg_range){
    .ptr = vertices.data(),
    .size = vertices.size() * sizeof(SokolRenderVertex),
});
```

Destroy:

```cpp
if (dst.vertex_buffer.id) sg_destroy_buffer(dst.vertex_buffer);
if (dst.index_buffer.id) sg_destroy_buffer(dst.index_buffer);
dst = {};
```

### Pipeline Definition

Generated shader header provides `voxov_scene_shader_desc`, `ATTR_voxov_scene_position`, and `UB_vs_params`.

```cpp
sg_shader shd = sg_make_shader(voxov_scene_shader_desc(sg_query_backend()));

pipelines_.opaque = sg_make_pipeline(&(sg_pipeline_desc){
    .shader = shd,
    .layout = {
        .attrs = {
            [ATTR_voxov_scene_position] = { .format = SG_VERTEXFORMAT_FLOAT3 },
            [ATTR_voxov_scene_color0] = { .format = SG_VERTEXFORMAT_FLOAT3 },
            [ATTR_voxov_scene_normal] = { .format = SG_VERTEXFORMAT_FLOAT3 },
        },
    },
    .index_type = SG_INDEXTYPE_UINT32,
    .cull_mode = SG_CULLMODE_BACK,
    .face_winding = SG_FACEWINDING_CCW,
    .depth = {
        .compare = SG_COMPAREFUNC_LESS_EQUAL,
        .write_enabled = true,
    },
    .label = "voxov-opaque-pipeline",
});

pipelines_.debug_xray = sg_make_pipeline(&(sg_pipeline_desc){
    .shader = shd,
    .layout.attrs = {
        [ATTR_voxov_scene_position] = { .format = SG_VERTEXFORMAT_FLOAT3 },
        [ATTR_voxov_scene_color0] = { .format = SG_VERTEXFORMAT_FLOAT3 },
        [ATTR_voxov_scene_normal] = { .format = SG_VERTEXFORMAT_FLOAT3 },
    },
    .index_type = SG_INDEXTYPE_UINT32,
    .cull_mode = SG_CULLMODE_NONE,
    .depth = { .compare = SG_COMPAREFUNC_ALWAYS, .write_enabled = false },
    .label = "voxov-debug-xray-pipeline",
});
```

### Pass Structure

Use current sokol pass model:

```cpp
const sg_pass_action action = {
    .colors = {
        [0] = {
            .load_action = SG_LOADACTION_CLEAR,
            .store_action = SG_STOREACTION_STORE,
            .clear_value = { 0.08f, 0.10f, 0.14f, 1.0f },
        },
    },
    .depth = {
        .load_action = SG_LOADACTION_CLEAR,
        .store_action = SG_STOREACTION_DONTCARE,
        .clear_value = 1.0f,
    },
};

sg_begin_pass(&(sg_pass){
    .action = action,
    .swapchain = sglue_swapchain(),
});

// World, debug, screen, ImGui.

sg_end_pass();
sg_commit();
```

### RenderScene to sokol Draws

```cpp
void SokolRenderer::draw_mesh(const SokolGpuMesh &mesh, const glm::mat4 &mvp) {
    if (!mesh.vertex_buffer.id || !mesh.index_buffer.id || mesh.index_count == 0) {
        return;
    }

    const vs_params_t vs_params = { .mvp = mvp };
    sg_apply_uniforms(UB_vs_params, &SG_RANGE(vs_params));
    sg_apply_bindings(&(sg_bindings){
        .vertex_buffers[0] = mesh.vertex_buffer,
        .index_buffer = mesh.index_buffer,
    });
    sg_draw(0, static_cast<int>(mesh.index_count), 1);
}

void SokolRenderer::render_frame(const RenderFrameContext &ctx,
                                 const RenderStats &stats,
                                 const RenderSurface &surface) {
    sg_begin_pass(&(sg_pass){ .action = pass_action_, .swapchain = sglue_swapchain() });

    const uint32_t view_count = std::max(1u, std::min(ctx.view_count, 2u));
    for (uint32_t i = 0; i < view_count; ++i) {
        const RenderView &view = ctx.views[i];
        const int vx = int(view.viewport.x * float(surface.width));
        const int vy = int(view.viewport.y * float(surface.height));
        const int vw = std::max(1, int(view.viewport.z * float(surface.width)));
        const int vh = std::max(1, int(view.viewport.w * float(surface.height)));

        sg_apply_viewport(vx, vy, vw, vh, true);
        const glm::mat4 vp = view.camera.projection(float(vw) / float(vh)) *
                             view.camera.view();

        sg_apply_pipeline(pipelines_.opaque);
        draw_mesh(transient_mesh_, vp);
        for (auto &[id, mesh] : cached_meshes_) {
            if (aabb_in_frustum(extract_frustum(vp), mesh.bounds_min, mesh.bounds_max)) {
                draw_mesh(mesh, vp);
            }
        }

        sg_apply_pipeline(pipelines_.debug_no_cull);
        draw_mesh(debug_grid_mesh_, vp);

        sg_apply_pipeline(ctx.debug_xray ? pipelines_.debug_xray : pipelines_.opaque);
        draw_mesh(debug_world_mesh_, vp);

        sg_apply_pipeline(pipelines_.screen);
        draw_mesh(debug_screen_mesh_, glm::mat4(1.0f));
    }

    render_imgui(stats, surface);
    sg_end_pass();
    sg_commit();
}
```

## Shader Strategy

Do not port inline GLSL strings. Move the current shader in `src/engine_render/gl_renderer.cpp:178-211` to `src/engine_render/shaders/voxov_scene.glsl`.

### Input Format

```glsl
@module voxov_scene
@ctype mat4 glm::mat4

@vs vs
layout(binding=0) uniform vs_params {
    mat4 mvp;
};

in vec3 position;
in vec3 color0;
in vec3 normal;

out vec3 v_color;
out vec3 v_normal;

void main() {
    v_color = color0;
    v_normal = normal;
    gl_Position = mvp * vec4(position, 1.0);
}
@end

@fs fs
in vec3 v_color;
in vec3 v_normal;
out vec4 frag_color;

void main() {
    float normal_len2 = dot(v_normal, v_normal);
    if (normal_len2 < 0.001) {
        frag_color = vec4(v_color, 1.0);
        return;
    }
    vec3 n = normalize(v_normal);
    float ndl = clamp(dot(n, normalize(vec3(0.3, 0.8, 0.4))), 0.0, 1.0);
    float stepped = floor(ndl * 4.0) / 4.0;
    float rim = pow(1.0 - max(dot(n, vec3(0.0, 0.0, 1.0)), 0.0), 2.0);
    vec3 base = v_color * (0.5 + 0.5 * stepped);
    frag_color = vec4(base + rim * 0.15, 1.0);
}
@end

@program voxov_scene vs fs
```

### Commands

Linux desktop GL:

```bash
dependencies/sokol-tools-bin/bin/linux/sokol-shdc \
  --input src/engine_render/shaders/voxov_scene.glsl \
  --output ${CMAKE_BINARY_DIR}/generated/voxov_scene.glsl.h \
  --slang glsl410 \
  --format sokol \
  --reflection
```

Web/Android GLES3:

```bash
sokol-shdc --input src/engine_render/shaders/voxov_scene.glsl \
  --output ${CMAKE_BINARY_DIR}/generated/voxov_scene.glsl.h \
  --slang glsl300es --format sokol --reflection
```

Multi-backend development build:

```bash
sokol-shdc --input src/engine_render/shaders/voxov_scene.glsl \
  --output ${CMAKE_BINARY_DIR}/generated/voxov_scene.glsl.h \
  --slang glsl410:glsl300es:hlsl5:metal_macos:wgsl \
  --format sokol --reflection
```

### Generated Header Use

```cpp
#include "voxov_scene.glsl.h"

sg_shader scene_shader = sg_make_shader(voxov_scene_shader_desc(sg_query_backend()));
sg_pipeline pipeline = sg_make_pipeline(&(sg_pipeline_desc){
    .shader = scene_shader,
    .layout.attrs = {
        [ATTR_voxov_scene_position].format = SG_VERTEXFORMAT_FLOAT3,
        [ATTR_voxov_scene_color0].format = SG_VERTEXFORMAT_FLOAT3,
        [ATTR_voxov_scene_normal].format = SG_VERTEXFORMAT_FLOAT3,
    },
});

const voxov_scene_vs_params_t params{ .mvp = mvp };
sg_apply_uniforms(UB_voxov_scene_vs_params, &SG_RANGE(params));
```

### CMake Rule

```cmake
set(VOXOV_SOKOL_SHADER_DIR "${CMAKE_BINARY_DIR}/generated/sokol")
file(MAKE_DIRECTORY "${VOXOV_SOKOL_SHADER_DIR}")

if(EMSCRIPTEN OR ANDROID)
    set(VOXOV_SOKOL_SLANG "glsl300es")
elseif(APPLE)
    set(VOXOV_SOKOL_SLANG "metal_macos:glsl410")
else()
    set(VOXOV_SOKOL_SLANG "glsl410")
endif()

set(VOXOV_SCENE_SHADER_IN "${CMAKE_SOURCE_DIR}/src/engine_render/shaders/voxov_scene.glsl")
set(VOXOV_SCENE_SHADER_OUT "${VOXOV_SOKOL_SHADER_DIR}/voxov_scene.glsl.h")

add_custom_command(
    OUTPUT "${VOXOV_SCENE_SHADER_OUT}"
    COMMAND sokol-shdc
        --input "${VOXOV_SCENE_SHADER_IN}"
        --output "${VOXOV_SCENE_SHADER_OUT}"
        --slang "${VOXOV_SOKOL_SLANG}"
        --format sokol
        --reflection
    DEPENDS "${VOXOV_SCENE_SHADER_IN}"
    VERBATIM
)

add_custom_target(voxov_sokol_shaders DEPENDS "${VOXOV_SCENE_SHADER_OUT}")

target_include_directories(engine_render_sokol PRIVATE "${VOXOV_SOKOL_SHADER_DIR}")
add_dependencies(engine_render_sokol voxov_sokol_shaders)
```

## Platform API Contract

Sokol owns the callback loop. The engine must stop assuming `poll_events()` and `native_window()`.

### New Platform Types

```cpp
enum class PlatformEventType : uint8_t {
    KeyDown,
    KeyUp,
    Char,
    MouseDown,
    MouseUp,
    MouseMove,
    MouseScroll,
    TouchBegin,
    TouchMove,
    TouchEnd,
    Resized,
    Focused,
    Unfocused,
    QuitRequested,
};

enum class PlatformKey : uint16_t {
    Unknown,
    W, A, S, D,
    I, J, K, L,
    Space, Escape, Enter,
    Up, Down, Left, Right,
    LeftShift, RightShift, LeftControl, RightControl,
    Q, R, E, F,
    F1, F2, F3, F4, F5, F11,
};

struct PlatformEvent {
    PlatformEventType type = PlatformEventType::KeyDown;
    PlatformKey key = PlatformKey::Unknown;
    uint32_t codepoint = 0;
    int mouse_button = 0;
    float x = 0.0f;
    float y = 0.0f;
    float dx = 0.0f;
    float dy = 0.0f;
    float scroll_x = 0.0f;
    float scroll_y = 0.0f;
    int width = 0;
    int height = 0;
};

struct PlatformInputSnapshot {
    std::bitset<512> keys_down;
    std::bitset<16> mouse_down;
    glm::vec2 mouse_pos{};
    glm::vec2 mouse_delta{};
    glm::vec2 scroll_delta{};
    bool focused = true;
};

class PlatformRuntime {
public:
    virtual ~PlatformRuntime() = default;
    virtual const PlatformInputSnapshot &input() const = 0;
    virtual RenderSurface surface() const = 0;
    virtual void set_title(const char *title) = 0;
    virtual void set_fullscreen(bool enabled) = 0;
    virtual bool fullscreen() const = 0;
    virtual void set_pointer_lock(bool enabled) = 0;
};
```

### Sokol Callback Wrapper

```cpp
class SokolPlatform final : public PlatformRuntime {
public:
    bool init(const PlatformCreateInfo &info);
    void on_init();
    void on_frame();
    void on_cleanup();
    void on_event(const sapp_event *ev);

    const PlatformInputSnapshot &input() const override { return input_; }
    RenderSurface surface() const override {
        return { sapp_width(), sapp_height(), sapp_dpi_scale() };
    }
    void set_title(const char *title) override { sapp_set_window_title(title); }
    void set_fullscreen(bool enabled) override { sapp_toggle_fullscreen(); fullscreen_ = enabled; }
    bool fullscreen() const override { return fullscreen_; }
    void set_pointer_lock(bool enabled) override { sapp_lock_mouse(enabled); }

private:
    void push_event(PlatformEvent event);
    PlatformKey map_key(sapp_keycode key) const;
    PlatformInputSnapshot input_{};
    std::vector<PlatformEvent> events_;
    std::unique_ptr<GameRuntime> runtime_;
    bool fullscreen_ = false;
};

static SokolPlatform g_platform;

static void voxov_init(void) { g_platform.on_init(); }
static void voxov_frame(void) { g_platform.on_frame(); }
static void voxov_cleanup(void) { g_platform.on_cleanup(); }
static void voxov_event(const sapp_event *ev) { g_platform.on_event(ev); }

sapp_desc sokol_main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return (sapp_desc){
        .init_cb = voxov_init,
        .frame_cb = voxov_frame,
        .cleanup_cb = voxov_cleanup,
        .event_cb = voxov_event,
        .width = 1280,
        .height = 720,
        .window_title = "VOXOV",
        .sample_count = 1,
        .swap_interval = 1,
        .high_dpi = true,
        .logger.func = slog_func,
    };
}
```

Important constraints:

- Do not call rendering API functions in `event_cb`.
- `sg_setup`, resource creation, rendering, `simgui_*`, and `sg_shutdown` run on the sokol callback thread.
- `sapp_get_swapchain()`/`sglue_swapchain()` is called once per rendered frame.
- `sapp_width()`/`sapp_height()` are framebuffer pixels, not logical window units.
- Pointer lock uses `sapp_lock_mouse`; on web it must be initiated from input-triggered flow.

## Dual Backend Strategy

Do not delete GL until parity is recorded.

### CMake Options

```cmake
option(VOXOV_RENDER_BACKEND_GL "Build OpenGL backend" ON)
option(VOXOV_RENDER_BACKEND_SOKOL "Build sokol backend" ON)
set(VOXOV_DEFAULT_RENDER_BACKEND "sokol" CACHE STRING "sokol or gl")

add_library(engine_render_core STATIC
    src/engine_render/renderer.cpp
    src/engine_render/debug_text.cpp
    src/engine_render/debug_draw/debug_draw.cpp
)
target_include_directories(engine_render_core PUBLIC src)
target_link_libraries(engine_render_core PUBLIC glm::glm fmt::fmt spdlog::spdlog)

if(VOXOV_RENDER_BACKEND_GL)
    add_library(engine_render_gl STATIC src/engine_render/gl_renderer.cpp)
    target_link_libraries(engine_render_gl PRIVATE engine_render_core glfw OpenGL::GL imgui_gl)
    target_compile_definitions(engine_render_gl PUBLIC VOXOV_HAS_RENDER_GL=1)
endif()

if(VOXOV_RENDER_BACKEND_SOKOL)
    add_library(engine_render_sokol STATIC
        src/engine_render/sokol_renderer.cpp
        src/engine_render/sokol_impl.cpp
    )
    target_link_libraries(engine_render_sokol PRIVATE engine_render_core imgui_sokol)
    target_compile_definitions(engine_render_sokol PUBLIC VOXOV_HAS_RENDER_SOKOL=1)
endif()
```

### Runtime Selection

```cpp
struct RendererCreateInfo {
    RenderBackendType backend = RenderBackendType::Sokol;
    RenderDeviceDesc device{};
};

bool Renderer::init(const RendererCreateInfo &info) {
    switch (info.backend) {
#if VOXOV_HAS_RENDER_SOKOL
    case RenderBackendType::Sokol:
        backend = std::make_unique<SokolRenderer>();
        break;
#endif
#if VOXOV_HAS_RENDER_GL
    case RenderBackendType::OpenGL:
        backend = std::make_unique<GLRenderer>();
        break;
#endif
    default:
        return false;
    }
    return backend->init(info.device);
}
```

### Command Line

```cpp
if (std::strcmp(renderer_name, "sokol") == 0) {
    renderer_backend = RenderBackendType::Sokol;
} else if (std::strcmp(renderer_name, "gl") == 0 ||
           std::strcmp(renderer_name, "opengl") == 0) {
    renderer_backend = RenderBackendType::OpenGL;
} else {
    std::fprintf(stderr, "Unknown renderer '%s'\n", renderer_name);
    return 2;
}
```

## Frame Pipeline

Current frame order in `src/game/game_runtime.cpp:65-118`:

```cpp
platform_adapter->poll_events();
input_frame = input_adapter->poll_input();
session_controller.handle_menu_input(...);
session_flow.update(engine, frame_dt);
engine.tick(frame_dt, EngineInputFrame{...});
runtime_stats = engine.stats();
```

Target order:

```cpp
void GameRuntime::frame(double frame_dt) {
    FrameInput input = hid_.begin_frame(platform_.input());

    // 1. Network receive before simulation so snapshots affect this tick.
    net_.receive_all();

    // 2. Menu/session flow consumes edge-trigger input.
    session_.update(input.ui, frame_dt);

    // 3. Fixed timeline.
    accumulator_ += std::min(frame_dt, 0.1);
    uint32_t fixed_steps = 0;
    while (accumulator_ >= k_fixed_dt && fixed_steps < k_max_fixed_steps) {
        const uint32_t tick = sim_tick_++;
        const InputState sim_input = hid_.sample_for_tick(tick);

        prediction_.record_input(tick, sim_input);
        gameplay_.pre_physics_tick(tick, sim_input, k_fixed_dt);
        physics_.step(k_fixed_dt);
        gameplay_.post_physics_tick(tick, k_fixed_dt);
        animation_.fixed_tick(tick, k_fixed_dt);
        replication_.build_and_send_commands(tick);

        accumulator_ -= k_fixed_dt;
        fixed_steps++;
    }

    // 4. Variable presentation timeline.
    const double alpha = accumulator_ / k_fixed_dt;
    net_.interpolate_remote_entities(alpha);
    animation_.presentation_update(frame_dt);
    audio_.update_listener(camera_);

    // 5. Extract render state after all simulation writes are done.
    RenderScene scene = presentation_.extract_scene(alpha);
    renderer_.upload_scene(scene);
    renderer_.render_frame(RenderFrameContext{
        .frame_index = frame_index_++,
        .alpha = alpha,
        .delta_seconds = frame_dt,
        .views = build_views(),
        .view_count = view_count_,
        .debug_xray = debug_.xray_enabled(),
    }, stats_, platform_.surface());
}
```

`sokol_app.frame_cb` calls exactly one `GameRuntime::frame`. No engine code above Platform Independence calls `sapp_*`.

## Entity/Component Contract

Rendering migration must not change game object identity, network identity, or replication history.

Current identity state:

- `src/engine/engine.hpp:180`: `ReplicatedPlayerMotion local_replication`
- `src/engine/engine.hpp:181`: `std::unordered_map<uint32_t, NetPlayerState> remote_players`
- `src/engine/engine.hpp:182`: `std::unordered_map<uint32_t, RemoteRenderPlayer> remote_render_players`
- `src/engine/engine.hpp:203-221`: latest snapshot and prediction history

Add stable handles before backend migration:

```cpp
struct EntityHandle {
    uint32_t index = 0;
    uint32_t generation = 0;
};

struct NetEntityId {
    uint32_t value = 0;
};

struct RenderObjectHandle {
    uint32_t index = 0;
    uint32_t generation = 0;
};

struct ReplicationState {
    NetEntityId net_id{};
    uint32_t last_snapshot_tick = 0;
    uint32_t last_ack_input_tick = 0;
    bool locally_predicted = false;
};

struct EntityRecord {
    EntityHandle entity{};
    NetEntityId net_id{};
    RenderObjectHandle render_object{};
    ReplicationState replication{};
};
```

Rules:

- `NetEntityId` is serialized. `EntityHandle` and `RenderObjectHandle` are local only.
- Renderer receives immutable extracted data, never `EntityHandle`.
- Prediction history stores `NetEntityId` and simulation state, not GPU handles.
- GPU resource recreation must not invalidate gameplay or network maps.

## Threading Model

### Ownership

| Thread | Owns | May not do |
|---|---|---|
| sokol callback/main thread | `sapp_*`, `sg_*`, `simgui_*`, renderer resources, ImGui | block on network IO |
| physics jobs | broadphase/narrowphase work on fixed tick buffers | call `sg_*`, mutate live render scene |
| networking thread or pump | ENet socket pump, packet queues | mutate gameplay world directly |
| resource IO jobs | file reads, decode/cook CPU data | create/destroy `sg_*` resources |
| audio callback/device thread | backend audio mixing only | lock gameplay/world/render mutexes |

### Synchronization

```cpp
template <typename T, size_t Capacity>
class SpscQueue {
public:
    bool push(const T &value);
    bool pop(T &value);
private:
    std::array<T, Capacity> values_{};
    std::atomic<uint32_t> head_{0};
    std::atomic<uint32_t> tail_{0};
};

struct GpuUploadRequest {
    ResourceId source_id{};
    std::span<const std::byte> cooked_bytes{};
    enum class Kind : uint8_t { Mesh, Texture } kind{};
};

struct RenderFrameExchange {
    std::mutex mutex;
    RenderScene pending_scene;
    uint64_t scene_generation = 0;
};
```

Resource upload rule:

```cpp
// Worker thread:
GpuUploadRequest req = cook_mesh_cpu(asset_bytes);
gpu_upload_queue.push(req);

// Sokol/main thread before render:
GpuUploadRequest req;
while (gpu_upload_queue.pop(req)) {
    renderer.create_or_replace_gpu_resource(req);
}
renderer.retire_deferred_destroys(frame_index - k_frames_in_flight);
```

Deferred destruction:

```cpp
struct DeferredDestroy {
    uint64_t retire_after_frame = 0;
    sg_buffer buffer{};
    sg_image image{};
    sg_shader shader{};
    sg_pipeline pipeline{};
};
```

## Phase Plan

### PR 1 - Inventory and Build Graph Guards

Files:

- Modify `CMakeLists.txt`
- Create `docs/technical-roadmap.md`

Before:

```cmake
target_link_libraries(engine_render PUBLIC OpenGL::GL glfw glm::glm fmt::fmt spdlog::spdlog imgui)
```

After:

```cmake
target_link_libraries(engine_render_core PUBLIC glm::glm fmt::fmt spdlog::spdlog)
target_link_libraries(engine_render_gl PRIVATE OpenGL::GL glfw imgui_gl)
```

Verify:

```bash
cmake -S . -B build -DVOXOV_BUILD_TESTS=ON -DVOXOV_RENDER_BACKEND_GL=ON -DVOXOV_RENDER_BACKEND_SOKOL=OFF
cmake --build build --target voxov voxov_server voxov_asset_cooker -j
rg -n "glfw|OpenGL::GL" build/CMakeCache.txt CMakeLists.txt
```

Gate:

- `voxov` still launches with `--renderer gl`.
- `voxov_server` links without GLFW/OpenGL.
- No public target above `platform_desktop_glfw` or `engine_render_gl` exposes GLFW/OpenGL.

Risk:

- Link breakage from targets relying on transitive GL/GLFW. Fix by moving deps to exact target, not restoring public leakage.

### PR 2 - Platform Event/Input Abstraction

Files:

- Create `src/platform/platform_events.hpp`
- Create `src/platform/platform_input_state.hpp`
- Modify `src/platform/desktop/input_desktop.*`
- Modify `src/game/desktop_runtime_adapter.*`
- Modify `src/game/main.cpp`

Before:

```cpp
const bool f11_down = glfwGetKey(platform.glfw_window(), GLFW_KEY_F11) == GLFW_PRESS;
```

After:

```cpp
const bool fullscreen_pressed = input_frame.primary.fullscreen_toggle_pressed;
if (fullscreen_pressed) {
    platform_adapter->toggle_fullscreen();
}
```

Verify:

```bash
cmake --build build --target voxov -j
./build/bin/voxov --renderer gl --devhud
```

Gate:

- F11 fullscreen, RMB pointer lock, WASD, mouse look, menu navigation, split-screen secondary keys behave like GL path before.

Risk:

- Edge-trigger regressions. Add a small input unit test for key down/up transitions.

### PR 3 - sokol Vendor and Implementation TU

Files:

- Add `dependencies/sokol/sokol_app.h`
- Add `dependencies/sokol/sokol_gfx.h`
- Add `dependencies/sokol/sokol_glue.h`
- Add `dependencies/sokol/sokol_log.h`
- Add `dependencies/sokol/util/sokol_imgui.h`
- Create `src/platform/sokol/sokol_impl.cpp`

Implementation TU:

```cpp
#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)
#define SOKOL_GLES3
#elif defined(_WIN32)
#define SOKOL_D3D11
#elif defined(__APPLE__)
#define SOKOL_METAL
#else
#define SOKOL_GLCORE
#endif

#define SOKOL_IMPL
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "sokol_log.h"

#include <imgui.h>
#include "sokol_imgui.h"
```

Verify:

```bash
cmake -S . -B build-sokol -DVOXOV_RENDER_BACKEND_SOKOL=ON -DVOXOV_RENDER_BACKEND_GL=ON
cmake --build build-sokol --target engine_render_sokol -j
```

Gate:

- Exactly one source file defines `SOKOL_IMPL`.
- No ODR/link duplicate symbol errors.

Risk:

- Backend define mismatch between `sokol_app`, `sokol_gfx`, and `sokol_imgui`. Keep all implementation includes in one TU.

### PR 4 - sokol App Shell Smoke

Files:

- Create `src/game/sokol_main.cpp`
- Create `src/platform/sokol/sokol_platform.hpp/.cpp`
- Add executable `voxov_sokol_smoke`

Smoke frame:

```cpp
static void init(void) {
    sg_setup(&(sg_desc){ .environment = sglue_environment(), .logger.func = slog_func });
}

static void frame(void) {
    sg_begin_pass(&(sg_pass){
        .action = {
            .colors[0] = {
                .load_action = SG_LOADACTION_CLEAR,
                .clear_value = { 0.1f, 0.1f, 0.1f, 1.0f },
            },
        },
        .swapchain = sglue_swapchain(),
    });
    sg_end_pass();
    sg_commit();
}

static void cleanup(void) { sg_shutdown(); }
```

Verify:

```bash
cmake --build build-sokol --target voxov_sokol_smoke -j
./build-sokol/bin/voxov_sokol_smoke
```

Gate:

- Window opens.
- Resize events update `sapp_width()`/`sapp_height()`.
- Vsync default is `swap_interval = 1`; `--no-vsync` sets `swap_interval = 0` for smoke executable.

Risk:

- `sokol_main` entry conflicts with existing `main`. Use a separate executable first.

### PR 5 - Shader Pipeline

Files:

- Create `src/engine_render/shaders/voxov_scene.glsl`
- Add CMake shader custom command
- Include generated header in sokol renderer only

Verify:

```bash
cmake --build build-sokol --target voxov_sokol_shaders -j
test -f build-sokol/generated/sokol/voxov_scene.glsl.h
```

Gate:

- Generated header compiles.
- `sg_make_shader(voxov_scene_shader_desc(sg_query_backend()))` succeeds with validation logging enabled.

Risk:

- Uniform layout mismatch. Use generated `*_params_t` structs only.

### PR 6 - Sokol RenderBackend Parity Scene

Files:

- Create `src/engine_render/sokol_renderer.hpp/.cpp`
- Modify `src/engine_render/renderer.*`
- Modify `src/engine/engine.cpp:164` init path to pass backend type

Before:

```cpp
renderer.init(window_handle);
```

After:

```cpp
renderer.init(RendererCreateInfo{
    .backend = runtime_options.renderer_backend,
    .device = platform_render_device_desc(),
});
```

Verify:

```bash
cmake --build build-sokol --target voxov_sokol -j
./build-sokol/bin/voxov_sokol --renderer sokol --devhud
./build-sokol/bin/voxov --renderer gl --devhud
```

Gate:

- Same `RenderScene` data renders through GL and sokol.
- Opaque meshes, debug grid, debug world, debug screen, split-screen viewports render.
- Screenshot parity: camera at spawn, `--spherical-planet`, `--debug-collision`, `--splitscreen`.

Risk:

- GL clip-space and face winding assumptions. Lock `SG_FACEWINDING_CCW` and compare screenshots.

### PR 7 - ImGui via sokol_imgui

Files:

- Create `src/engine_ui/imgui_sokol_bridge.hpp/.cpp`
- Modify `src/engine_render/sokol_renderer.cpp`
- Modify `src/platform/sokol/sokol_platform.cpp`

Event:

```cpp
void SokolPlatform::on_event(const sapp_event *ev) {
    const bool imgui_captured = simgui_handle_event(ev);
    if (!imgui_captured) {
        push_event(map_sokol_event(ev));
    }
}
```

Frame:

```cpp
simgui_new_frame(&(simgui_frame_desc_t){
    .width = sapp_width(),
    .height = sapp_height(),
    .delta_time = sapp_frame_duration(),
    .dpi_scale = sapp_dpi_scale(),
});

draw_dev_ui(stats);
simgui_render(); // before sg_end_pass()
```

Verify:

```bash
cmake --build build-sokol --target voxov_sokol -j
./build-sokol/bin/voxov_sokol --renderer sokol --devhud
```

Gate:

- Keyboard focus goes to ImGui when it captures input.
- DPI scale is correct on high-DPI displays.
- No ImGui call outside sokol/renderer thread.

Risk:

- UI currently disabled behind `if (false && ...)` in `gl_renderer.cpp:693`; decide whether the sokol path should keep this disabled or expose dev UI.

### PR 8 - Full Runtime Through sokol_app

Files:

- Create final `voxov_sokol` executable
- Modify `src/game/game_runtime.cpp` to accept callback-driven platform
- Keep `voxov` GL executable until parity signoff

Verify:

```bash
cmake --build build-sokol --target voxov_sokol voxov voxov_server -j
./build-sokol/bin/voxov_sokol --renderer sokol --devhud --spherical-planet
./build-sokol/bin/voxov --renderer gl --devhud --spherical-planet
./build-sokol/bin/voxov_server --port 7777
```

Gate:

- Startup, shutdown, resize, fullscreen, pointer lock, menu, local session, LAN session, debug toggles pass.
- GL path still works.

Risk:

- Current `GameRuntime` assumes `platform_adapter->poll_events()` at `game_runtime.cpp:72`. Sokol adapter must make this a no-op and input must already be event-fed.

### PR 9 - Audio Boundary, No Backend Swap Yet

Files:

- Create `src/engine_audio/audio_backend.hpp`
- Rename current implementation to `engine_audio_miniaudio`
- Keep `UiAudio` API stable

Before:

```cpp
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>
```

After:

```cpp
class IAudioBackend {
public:
    virtual ~IAudioBackend() = default;
    virtual bool init() = 0;
    virtual void shutdown() = 0;
    virtual SoundHandle create_sound(std::span<const std::byte> wav) = 0;
    virtual void play(SoundHandle sound) = 0;
};
```

Verify:

```bash
cmake --build build-sokol --target voxov_sokol -j
./build-sokol/bin/voxov_sokol --renderer sokol
```

Gate:

- UI move/click sounds still play.
- miniaudio include path is private to audio backend target.

Risk:

- Replacing miniaudio with `sokol_audio` loses decoding and `ma_sound` convenience. Do not do it until a mixer/decoder plan exists.

## Testing Gates Per Phase

### Build Gates

```bash
cmake -S . -B build-gl -DVOXOV_RENDER_BACKEND_GL=ON -DVOXOV_RENDER_BACKEND_SOKOL=OFF -DVOXOV_BUILD_TESTS=ON
cmake --build build-gl --target voxov voxov_server voxov_asset_cooker -j

cmake -S . -B build-sokol -DVOXOV_RENDER_BACKEND_GL=ON -DVOXOV_RENDER_BACKEND_SOKOL=ON -DVOXOV_BUILD_TESTS=ON
cmake --build build-sokol --target voxov voxov_sokol voxov_server voxov_asset_cooker -j
```

Expected:

- Both builds complete.
- `voxov_server` has no GLFW/OpenGL link dependency.
- `engine_render_core` has no sokol, GLFW, or OpenGL dependency.

### Runtime Gates

```bash
./build-gl/bin/voxov --renderer gl --devhud --spherical-planet
./build-sokol/bin/voxov_sokol --renderer sokol --devhud --spherical-planet
./build-sokol/bin/voxov_sokol --renderer sokol --splitscreen
./build-sokol/bin/voxov_sokol --renderer sokol --debug-collision --debug-xray
```

Expected:

- Spawn scene visible.
- FPS/title stats update.
- No validation errors from `slog_func`.
- Resize does not stretch view incorrectly.
- Fullscreen toggle preserves rendering.

### Visual Parity

Capture:

```bash
./tools/capture_frame.sh build-gl/bin/voxov "gl_spawn.png" --renderer gl --devhud
./tools/capture_frame.sh build-sokol/bin/voxov_sokol "sokol_spawn.png" --renderer sokol --devhud
./tools/compare_images.py gl_spawn.png sokol_spawn.png --max-rmse 0.06
```

Views:

- spawn flat world
- spawn spherical planet
- debug collision
- split-screen
- menu open

### Performance Baselines

Record in `docs/perf-baselines/sokol-migration.csv`:

```text
backend,scene,resolution,fps,cpu_ms,render_cpu_ms,draw_calls,vertices,indices,commit_ms
gl,spawn,1280x720,,,,,,,
sokol,spawn,1280x720,,,,,,,
```

Gate:

- `sokol` FPS within 10 percent of GL on spawn scene before deleting GL.
- CPU render submission not worse than 20 percent unless explained by validation/debug mode.

### Network/Gameplay Gates

```bash
cmake --build build-sokol --target voxov_server voxov_sokol -j
./build-sokol/bin/voxov_server --port 7777
./build-sokol/bin/voxov_sokol --renderer sokol --connect 127.0.0.1 --port 7777
```

Expected:

- Client connects.
- `RenderStats.net_connected == true`.
- Remote player maps remain keyed by `NetEntityId`/`uint32_t`, not render handles.

## Cutover Criteria

Remove GL/GLFW from the default build only after:

```bash
rg -n "GLFW|glfw|OpenGL::GL|<GL/|<OpenGL/|imgui_impl_glfw|imgui_impl_opengl3" src CMakeLists.txt
```

shows only:

- `src/engine_render/gl_renderer.*`
- `src/platform/glfw/*` or legacy GL target files
- CMake guarded by `VOXOV_RENDER_BACKEND_GL`
- docs explicitly marked legacy

Do not delete Android/Web raw GL paths until a separate PR either ports them to sokol or marks them legacy with build gates.

