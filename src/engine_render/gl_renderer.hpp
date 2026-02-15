#pragma once

#include "engine_render/render_backend.hpp"

struct GLFWwindow;

class GLRenderer : public IRenderBackend {
public:
    void init(void *window_handle) override;
    void shutdown() override;
    void upload_scene(const RenderScene &scene) override;
    void update_overlay_text(const RenderMesh &overlay) override;
    void begin_frame(const RenderFrameContext &ctx, const Camera &camera, const RenderStats &stats) override;
    void end_frame() override;

private:
    void draw_mesh(const RenderMesh &mesh) const;

    GLFWwindow *window = nullptr;
    RenderScene scene;
};
