#pragma once

#include <array>

#include "engine_math/camera.hpp"
#include "engine_render/render_backend_type.hpp"
#include "engine_render/render_types.hpp"
#include "platform/platform_input_state.hpp"  // RenderSurface

// ---------------------------------------------------------------------------
// Backend-neutral render contract.
// Window/surface info and GPU device desc are passed explicitly so no
// native window pointer leaks into the interface.
// ---------------------------------------------------------------------------

struct RenderDeviceDesc {
    RenderBackendType backend = RenderBackendType::Sokol;
    int color_format = 0;   // sg_pixel_format for sokol path
    int depth_format = 0;   // sg_pixel_format for sokol path
    int sample_count = 1;
};

struct RenderView {
    Camera camera{};
    // Normalized viewport rectangle: x, y, width, height in [0, 1].
    glm::vec4 viewport = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
};

struct RenderFrameContext {
    uint64_t frame_index = 0;
    double alpha = 0.0;
    double delta_seconds = 0.0;
    float aspect_ratio = 16.0f / 9.0f;
    CameraRelativeOrigin camera_origin{};
    std::array<RenderView, 2> views{};
    uint32_t view_count = 1;
    bool debug_xray = false;
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
                                RenderStats &stats,
                                const RenderSurface &surface) = 0;

    // Captures the current framebuffer to a PNG file at the given path.
    // Returns true on success. Only supported on OpenGL backends (GLES3/GL).
    virtual bool capture_screenshot(const char *filepath,
                                    int width, int height) = 0;
};
