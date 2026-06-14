#include "engine_render/renderer.hpp"

#include "engine_render/sokol_renderer.hpp"

#include <memory>
#include <cstdio>

bool Renderer::init(const RendererCreateInfo &info) {
    switch (info.backend) {
    case RenderBackendType::Sokol: {
        auto sokol = std::make_unique<SokolRenderer>();
        if (!sokol->init(info.device_desc)) {
            std::fprintf(stderr, "Renderer::init: Sokol backend init failed\n");
            return false;
        }
        backend = std::move(sokol);
        return true;
    }
    default:
        std::fprintf(stderr, "Renderer::init: no backend available\n");
        return false;
    }
}

void Renderer::shutdown() {
    if (backend) {
        backend->shutdown();
        backend.reset();
    }
}

void Renderer::upload_scene(const RenderScene &scene) {
    if (!backend) {
        return;
    }
    backend->upload_scene(scene);
}

void Renderer::update_dynamic_meshes(const RenderMesh &debug_world,
                                     const RenderMesh &debug_screen) {
    if (!backend) {
        return;
    }
    backend->update_dynamic_meshes(debug_world, debug_screen);
}

void Renderer::render_frame(const RenderFrameContext &ctx,
                              RenderStats &stats,
                              const RenderSurface &surface) {
    cached_surface_ = surface;
    if (!backend) {
        return;
    }
    backend->render_frame(ctx, stats, surface);
}

void Renderer::begin_frame(const RenderFrameContext &ctx,
                           RenderStats &stats) {
    // Legacy path: delegates to the backend's begin_frame if available.
    // For sokol backends, this is a no-op (render_frame is the primary API).
    if (!backend) {
        return;
    }
    backend->render_frame(ctx, stats, cached_surface_);
}

void Renderer::end_frame() {
    // Legacy path: no-op. Frame is committed in render_frame().
}

bool Renderer::capture_screenshot(const char *filepath, int width, int height) {
    if (!backend) return false;
    return backend->capture_screenshot(filepath, width, height);
}
