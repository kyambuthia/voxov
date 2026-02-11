#pragma once

#include <cstdint>
#include <memory>

struct RenderFrameContext {
    uint64_t frame_index = 0;
    double alpha = 0.0;
};

class Renderer {
public:
    void init(void *window_handle);
    void shutdown();
    void begin_frame(const RenderFrameContext &ctx);
    void render_world();
    void end_frame();

private:
    class VulkanRenderer;
    std::unique_ptr<VulkanRenderer> backend;
};
