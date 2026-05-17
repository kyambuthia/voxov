#pragma once

#include "engine_render/render_backend.hpp"

#include <cstdint>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <unordered_map>

struct GLFWwindow;

class GLRenderer : public IRenderBackend {
public:
    // Legacy init path: takes a GLFW window handle directly.
    void init(void *window_handle);

    // New IRenderBackend contract.
    bool init(const RenderDeviceDesc &desc) override;
    void shutdown() override;
    void upload_scene(const RenderScene &scene) override;
    void update_dynamic_meshes(const RenderMesh &debug_world,
                               const RenderMesh &debug_screen) override;
    void render_frame(const RenderFrameContext &ctx,
                      const RenderStats &stats,
                      const RenderSurface &surface) override;

private:
    struct UploadedMesh {
        unsigned int vertex_array = 0;
        unsigned int vertex_buffer = 0;
        unsigned int index_buffer = 0;
        uint32_t index_count = 0;
        glm::vec3 bounds_min{};
        glm::vec3 bounds_max{};
        uint8_t material = 0;
    };

    bool init_pipeline();
    void shutdown_pipeline();
    void destroy_uploaded_mesh(UploadedMesh &mesh);
    void upload_mesh(UploadedMesh &mesh, const RenderMesh &source,
                     unsigned int usage = 0x88B4 /*GL_STATIC_DRAW*/);
    void draw_mesh(const UploadedMesh &mesh, const glm::mat4 &mvp);

    // Dirty-bit constants for GL state change detection
    static constexpr uint32_t kDirtyProgram = 1u << 0;

    GLFWwindow *window = nullptr;
    RenderScene scene;
    bool imgui_ready = false;
    unsigned int program = 0;
    UploadedMesh transient_mesh;
    UploadedMesh debug_grid_mesh;
    UploadedMesh debug_world_mesh;
    UploadedMesh debug_screen_mesh;
    std::unordered_map<uint64_t, UploadedMesh> cached_meshes_;
    int uniform_mvp = -1;
    uint64_t last_debug_world_hash = 0;
    uint64_t last_debug_screen_hash = 0;
    bool has_dynamic_mesh_hash = false;

    // Dirty-bit state tracking
    uint32_t dirty_flags_ = ~0u;
    glm::mat4 last_mvp_{1.0f};
    bool last_cull_face_enabled_ = true;
    bool last_depth_test_enabled_ = true;

    // Surface state from the new contract
    RenderSurface surface_{};
};
