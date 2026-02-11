#include "engine_render/renderer.hpp"
#include "engine_render/vulkan_renderer.hpp"

#include <memory>

void Renderer::init(void *window_handle) {
    backend = std::make_unique<VulkanRenderer>();
    backend->init(window_handle);
}

void Renderer::shutdown() {
    if (backend) {
        backend->shutdown();
        backend.reset();
    }
}

void Renderer::begin_frame(const RenderFrameContext &ctx) {
    backend->begin_frame(ctx);
}

void Renderer::render_world() {
    backend->render_world();
}

void Renderer::end_frame() {
    backend->end_frame();
}
