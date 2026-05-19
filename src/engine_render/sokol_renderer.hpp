#pragma once

#include "engine_render/render_backend.hpp"

#include <cstdint>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <unordered_map>

#include "sokol_gfx.h"

struct SokolRenderVertex {
    float px, py, pz;
    float cr, cg, cb;
    float nx, ny, nz;
};

struct SokolGpuMesh {
    sg_buffer vertex_buffer{};
    sg_buffer index_buffer{};
    uint32_t index_count = 0;
    glm::vec3 bounds_min{};
    glm::vec3 bounds_max{};
    uint8_t material = 0;
};

struct SokolPipelines {
    sg_shader scene_shader{};
    sg_pipeline opaque{};
    sg_pipeline debug_no_cull{};
    sg_pipeline debug_xray{};
    sg_pipeline screen{};
};

class SokolRenderer final : public IRenderBackend {
public:
    bool init(const RenderDeviceDesc &desc) override;
    void shutdown() override;
    void upload_scene(const RenderScene &scene) override;
    void update_dynamic_meshes(const RenderMesh &debug_world,
                               const RenderMesh &debug_screen) override;
    void render_frame(const RenderFrameContext &ctx,
                      const RenderStats &stats,
                      const RenderSurface &surface) override;

private:
    void upload_mesh(SokolGpuMesh &dst, const RenderMesh &src, bool stream);
    void destroy_mesh(SokolGpuMesh &mesh);
    void draw_mesh(const SokolGpuMesh &mesh, const glm::mat4 &mvp);

    bool setup_pipelines();

    SokolPipelines pipelines_{};
    SokolGpuMesh transient_mesh_{};
    SokolGpuMesh debug_grid_mesh_{};
    SokolGpuMesh debug_world_mesh_{};
    SokolGpuMesh debug_screen_mesh_{};
    SokolGpuMesh debug_triangle_mesh_{};
    std::unordered_map<uint64_t, SokolGpuMesh> cached_meshes_;
    sg_pass_action pass_action_{};

    // Hash tokens for O(1) change detection on dynamic meshes.
    uint64_t last_debug_world_hash_ = 0;
    uint64_t last_debug_screen_hash_ = 0;
    bool has_dynamic_mesh_hash_ = false;
};
