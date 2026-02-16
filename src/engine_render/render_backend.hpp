#pragma once

#include <array>

#include "engine_math/camera.hpp"
#include "engine_render/render_types.hpp"

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
    std::array<RenderView, 2> views{};
    uint32_t view_count = 1;
};

class IRenderBackend {
public:
    virtual ~IRenderBackend() = default;

    virtual void init(void *window_handle) = 0;
    virtual void shutdown() = 0;

    virtual void upload_scene(const RenderScene &scene) = 0;
    virtual void update_overlay_text(const RenderMesh &overlay) = 0;
    virtual void begin_frame(const RenderFrameContext &ctx, const RenderStats &stats) = 0;
    virtual void end_frame() = 0;
};
