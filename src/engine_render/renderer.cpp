#include "engine_render/renderer.hpp"
#include "engine_render/gl_renderer.hpp"
#include "engine_render/vulkan_renderer.hpp"

#include <memory>

void Renderer::init(void *window_handle, RenderBackendType backend_type) {
    if (backend_type == RenderBackendType::OpenGL) {
        backend = std::make_unique<GLRenderer>();
    } else {
        backend = std::make_unique<VulkanRenderer>();
    }
    backend->init(window_handle);
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

void Renderer::update_overlay_text(const RenderMesh &overlay) {
    backend->update_overlay_text(overlay);
}

void Renderer::begin_frame(const RenderFrameContext &ctx, const RenderStats &stats) {
    backend->begin_frame(ctx, stats);
}

void Renderer::end_frame() {
    backend->end_frame();
}
