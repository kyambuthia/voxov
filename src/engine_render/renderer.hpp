#pragma once

#include <memory>

#include "engine_render/render_backend.hpp"
#include "platform/platform.hpp"

class Renderer {
public:
    void init(void *window_handle, RenderBackendType backend_type);
    void shutdown();
    void upload_scene(const RenderScene &scene);
    void update_dynamic_meshes(const RenderMesh &debug_world, const RenderMesh &debug_screen);
    void begin_frame(const RenderFrameContext &ctx, const RenderStats &stats);
    void end_frame();

private:
    std::unique_ptr<IRenderBackend> backend;
};
