#pragma once

#include <memory>

#include "engine_render/render_backend.hpp"
#include "platform/platform.hpp"

class Renderer {
public:
    void init(void *window_handle, RenderBackendType backend_type);
    void shutdown();
    void upload_scene(const RenderScene &scene);
    void update_overlay_text(const RenderMesh &overlay);
    void begin_frame(const RenderFrameContext &ctx, const Camera &camera, const RenderStats &stats);
    void end_frame();

private:
    std::unique_ptr<IRenderBackend> backend;
};
