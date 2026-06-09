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
    size_t vertex_buffer_size = 0;
    size_t index_buffer_size = 0;
    uint32_t index_count = 0;
    sg_index_type index_type = SG_INDEXTYPE_UINT32;
    glm::vec3 bounds_min{};
    glm::vec3 bounds_max{};
    uint8_t material = 0;
};

struct SokolPipelines {
    sg_shader scene_shader{};
    sg_pipeline opaque{};
    sg_pipeline opaque_u16{};
    sg_pipeline wireframe{};          // SG_PRIMITIVETYPE_LINES
    sg_pipeline wireframe_u16{};
    sg_pipeline debug_no_cull{};
    sg_pipeline debug_no_cull_u16{};
    sg_pipeline debug_xray{};
    sg_pipeline debug_xray_u16{};
    sg_pipeline screen{};
    sg_pipeline screen_u16{};
};

struct SokolDirectionalLight {
    glm::vec3 direction{0.318f, 0.848f, 0.424f};
    glm::vec3 ambient{0.28f, 0.34f, 0.48f};
    glm::vec3 diffuse{1.0f, 0.88f, 0.62f};
    glm::vec3 specular{0.55f, 0.58f, 0.65f};
};

struct SokolMaterial {
    glm::vec3 ambient{1.0f, 1.0f, 1.0f};
    glm::vec3 diffuse{1.0f, 1.0f, 1.0f};
    glm::vec3 specular{0.32f, 0.32f, 0.34f};
    float shininess = 32.0f;
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
    void draw_mesh(const SokolGpuMesh &mesh,
                   const glm::mat4 &mvp,
                   const glm::mat4 &model,
                   const glm::vec3 &camera_pos,
                   const glm::dvec3 &camera_relative_origin = glm::dvec3(0.0),
                   sg_pipeline pipeline_u32 = {},
                   sg_pipeline pipeline_u16 = {});
    void draw_wireframe(const SokolGpuMesh &mesh,
                        const glm::mat4 &mvp);

    bool setup_pipelines();

    SokolPipelines pipelines_{};
    SokolGpuMesh transient_mesh_{};
    SokolGpuMesh wireframe_mesh_{};
    SokolGpuMesh debug_world_mesh_{};
    SokolGpuMesh debug_screen_mesh_{};
    std::unordered_map<uint64_t, SokolGpuMesh> cached_meshes_;
    sg_pass_action pass_action_{};
    SokolDirectionalLight light_{};
    SokolMaterial material_{};

    // Hash tokens for O(1) change detection on dynamic meshes.
    uint64_t last_debug_world_hash_ = 0;
    uint64_t last_debug_screen_hash_ = 0;
    uint64_t last_wireframe_hash_ = 0;
    bool has_dynamic_mesh_hash_ = false;
};
