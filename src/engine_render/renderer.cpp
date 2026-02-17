#include "engine_render/renderer.hpp"
#include "engine_render/gl_renderer.hpp"
#if VOXOV_ENABLE_VULKAN
#include "engine_render/vulkan_renderer.hpp"
#endif

#include <memory>

void Renderer::init(void *window_handle, RenderBackendType backend_type) {
    if (backend_type == RenderBackendType::OpenGL) {
        backend = std::make_unique<GLRenderer>();
    } else {
#if VOXOV_ENABLE_VULKAN
        backend = std::make_unique<VulkanRenderer>();
#else
        backend = std::make_unique<GLRenderer>();
#endif
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

void Renderer::update_dynamic_meshes(const RenderMesh &debug_world, const RenderMesh &debug_screen) {
    backend->update_dynamic_meshes(debug_world, debug_screen);
}

void Renderer::begin_frame(const RenderFrameContext &ctx, const RenderStats &stats) {
    backend->begin_frame(ctx, stats);
}

void Renderer::end_frame() {
    backend->end_frame();
}
