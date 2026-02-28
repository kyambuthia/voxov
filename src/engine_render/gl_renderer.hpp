#pragma once

#include "engine_render/render_backend.hpp"

#include <glm/mat4x4.hpp>

struct GLFWwindow;

class GLRenderer : public IRenderBackend {
public:
    void init(void *window_handle) override;
    void shutdown() override;
    void upload_scene(const RenderScene &scene) override;
    void update_dynamic_meshes(const RenderMesh &debug_world, const RenderMesh &debug_screen) override;
    void begin_frame(const RenderFrameContext &ctx, const RenderStats &stats) override;
    void end_frame() override;

private:
    bool init_pipeline();
    void shutdown_pipeline();
    void draw_mesh(const RenderMesh &mesh, const glm::mat4 &mvp) const;

    GLFWwindow *window = nullptr;
    RenderScene scene;
    bool imgui_ready = false;
    unsigned int program = 0;
    unsigned int vertex_buffer = 0;
    unsigned int index_buffer = 0;
    int uniform_mvp = -1;
};
