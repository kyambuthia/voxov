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
    // WHY direction (0.58, 0.58, 0.58): Previous value (0, 0.3, 0.954) was
    // nearly perpendicular to equatorial surface normals → zero diffuse
    // lighting → terrain looked flat. A direction with equal components in
    // all axes guarantees NdL ≈ 0.58 for any cube-face surface normal,
    // producing strong diffuse contrast that reveals height variation.
    glm::vec3 direction{0.577f, 0.577f, 0.577f};
    glm::vec3 ambient{0.12f, 0.13f, 0.16f};
    glm::vec3 diffuse{1.3f, 1.15f, 0.85f};
    glm::vec3 specular{0.45f, 0.48f, 0.55f};
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
                      RenderStats &stats,
                      const RenderSurface &surface) override;

    bool capture_screenshot(const char *filepath,
                            int width, int height) override;

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

    // ── Atmosphere per-frame uniforms ──────────────────────────────────
    // Packed std140 uniform block uploaded at binding 2 each frame.
    // Set from RenderFrameContext::atmosphere in render_frame().
    struct atm_params_t {
        glm::vec4 planet_center_radius{0,0,0,500};
        glm::vec4 atm_params_1{50,8000,1200,0.76};
        glm::vec4 rayleigh_scatter{5.8e-6,13.5e-6,33.1e-6,0};
        glm::vec4 mie_scatter{21e-5,0,0,0};
        glm::vec4 sun_dir_intensity{0,1,0,20};
    };
    atm_params_t atm_uniforms_{};

    // Hash tokens for O(1) change detection on dynamic meshes.
    uint64_t last_debug_world_hash_ = 0;
    uint64_t last_debug_screen_hash_ = 0;

    // ── Wireframe upload deduplication ────────────────────────────────
    // Wireframe meshes were merged+uploaded every frame regardless of
    // content. At planet scale, the wireframe grid (64 cells/face,
    // ~101K verts, ~2.8 MB) saturated GPU upload bandwidth → 1 FPS.
    // Hash of mesh_id set detects when wireframe meshes actually change;
    // upload skipped on hash match (retains previous GPU buffer).
    uint64_t last_wireframe_hash_ = 0;

    bool has_dynamic_mesh_hash_ = false;
};
