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

    // ── Atmosphere parameters (packed for GPU std140 uniform block) ────
    // Set by Engine each frame.  Passed to fragment shader for aerial
    // perspective (Rayleigh + Mie atmospheric transmittance).
    struct AtmosphereUniformBlock {
        glm::vec4 planet_center_radius{0,0,0,500};  // xyz=center, w=radius
        glm::vec4 atm_params_1{50,8000,1200,0.76}; // x=atm_h, y=H_R, z=H_M, w=g
        glm::vec4 rayleigh_scatter{5.8e-6, 13.5e-6, 33.1e-6, 0};
        glm::vec4 mie_scatter{21e-5, 0, 0, 0};      // x=β_M
        glm::vec4 sun_dir_intensity{0,1,0,20};       // xyz=sun_dir, w=intensity
        glm::vec4 sky_color{0.08f, 0.10f, 0.14f, 1.0f}; // HDR sky color for clear
    };
    AtmosphereUniformBlock atmosphere{};
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
