#include "engine_render/renderer.hpp"

#if VOXOV_HAS_RENDER_GL
#include "engine_render/gl_renderer.hpp"
#endif
#if VOXOV_HAS_RENDER_SOKOL
#include "engine_render/sokol_renderer.hpp"
#endif

#include <memory>
#include <cstdio>

void Renderer::init(const RendererCreateInfo &info) {
    switch (info.backend) {
#if VOXOV_HAS_RENDER_GL
    case RenderBackendType::OpenGL: {
        auto gl = std::make_unique<GLRenderer>();
        gl->init(info.window_handle);
        backend = std::move(gl);
        break;
    }
#endif
#if VOXOV_HAS_RENDER_SOKOL
    case RenderBackendType::Sokol: {
        auto sokol = std::make_unique<SokolRenderer>();
        if (!sokol->init(info.device_desc)) {
            std::fprintf(stderr, "Renderer::init: Sokol backend init failed\n");
            return;
        }
        backend = std::move(sokol);
        break;
    }
#endif
    default:
        std::fprintf(stderr, "Renderer::init: no backend available\n");
        return;
    }
}

void Renderer::shutdown() {
    if (backend) {
        backend->shutdown();
        backend.reset();
    }
}

void Renderer::upload_scene(const RenderScene &scene) {
    backend->upload_scene(scene);
}

void Renderer::update_dynamic_meshes(const RenderMesh &debug_world,
                                     const RenderMesh &debug_screen) {
    backend->update_dynamic_meshes(debug_world, debug_screen);
}

void Renderer::render_frame(const RenderFrameContext &ctx,
                             const RenderStats &stats,
                             const RenderSurface &surface) {
    cached_surface_ = surface;
    backend->render_frame(ctx, stats, surface);
}

void Renderer::begin_frame(const RenderFrameContext &ctx,
                           const RenderStats &stats) {
    // Legacy path: delegates to the backend's begin_frame if available.
    // For sokol backends, this is a no-op (render_frame is the primary API).
    backend->render_frame(ctx, stats, cached_surface_);
}

void Renderer::end_frame() {
    // Legacy path: no-op. Frame is committed in render_frame().
}
