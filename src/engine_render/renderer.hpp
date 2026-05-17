#pragma once

#include <memory>

#include "engine_render/render_backend.hpp"

struct RendererCreateInfo {
    RenderBackendType backend = RenderBackendType::Sokol;
    void *window_handle = nullptr;
    RenderDeviceDesc device_desc{};
};

class Renderer {
public:
    void init(const RendererCreateInfo &info);
    void shutdown();
    void upload_scene(const RenderScene &scene);
    void update_dynamic_meshes(const RenderMesh &debug_world,
                               const RenderMesh &debug_screen);
    void render_frame(const RenderFrameContext &ctx,
                      const RenderStats &stats,
                      const RenderSurface &surface);

    // Legacy compatibility during migration.
    void begin_frame(const RenderFrameContext &ctx, const RenderStats &stats);
    void end_frame();

private:
    std::unique_ptr<IRenderBackend> backend;
    RenderSurface cached_surface_{};
};
