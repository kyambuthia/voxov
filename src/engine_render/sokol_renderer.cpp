#include "engine_render/sokol_renderer.hpp"
#include "engine_render/voxel_texture_data.hpp"

#include "sokol_gfx.h"
#if !defined(VOXOV_PLATFORM_ANDROID)
#include "sokol_app.h"
#include "sokol_glue.h"
#endif
#include "sokol_log.h"

#if defined(SOKOL_GLCORE)
#include <GL/gl.h>
#elif defined(SOKOL_GLES3)
#include <GLES3/gl3.h>
#endif

#define STB_IMAGE_WRITE_IMPLEMENTATION
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#include "stb_image_write.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>
#include <vector>

#include <glm/gtc/type_ptr.hpp>
#include <glm/vec4.hpp>

#define SOKOL_SHDC_IMPL
#include "voxov_scene.glsl.h"

namespace {

// O(1) change-detection token: vertex count + index count.
uint64_t mesh_size_token(const RenderMesh &mesh) {
    const size_t index_count =
        mesh.use_16_bit_indices ? mesh.indices16.size() : mesh.indices.size();
    return (static_cast<uint64_t>(mesh.vertices.size()) << 32) |
           static_cast<uint64_t>(index_count);
}

// --- Frustum culling (same as GL renderer) ---

struct Frustum { glm::vec4 planes[6]; };

Frustum extract_frustum(const glm::mat4 &vp) {
    auto row = [&](int i) {
        return glm::vec4(vp[0][i], vp[1][i], vp[2][i], vp[3][i]);
    };
    const glm::vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
    Frustum f{};
    f.planes[0] = r3 + r0; // left
    f.planes[1] = r3 - r0; // right
    f.planes[2] = r3 + r1; // bottom
    f.planes[3] = r3 - r1; // top
    f.planes[4] = r3 + r2; // near
    f.planes[5] = r3 - r2; // far
    return f;
}

bool aabb_in_frustum(const Frustum &f, glm::vec3 bmin, glm::vec3 bmax) {
    for (int p = 0; p < 6; ++p) {
        const glm::vec4 &pl = f.planes[p];
        const glm::vec3 pv(
            (pl.x >= 0.0f) ? bmax.x : bmin.x,
            (pl.y >= 0.0f) ? bmax.y : bmin.y,
            (pl.z >= 0.0f) ? bmax.z : bmin.z);
        if (glm::dot(glm::vec3(pl), pv) + pl.w < 0.0f) return false;
    }
    return true;
}

// M4x4 → sg_range for uniform upload
struct vs_params_t {
    glm::mat4 mvp;
    glm::mat4 model;
};

struct fs_params_t {
    glm::vec4 light_direction;
    glm::vec4 light_ambient;
    glm::vec4 light_diffuse;
    glm::vec4 light_specular;
    glm::vec4 material_ambient;
    glm::vec4 material_diffuse;
    glm::vec4 material_specular_shininess;
    glm::vec4 camera_pos;
};

// Atmosphere uniform block (binding 2, std140).
// Mirrors the AtmosphereUniforms struct from atmosphere.hpp.
struct atm_params_t {
    glm::vec4 planet_center_radius;      // xyz=center, w=radius
    glm::vec4 atm_params_1;              // x=atm_h, y=H_R, z=H_M, w=g
    glm::vec4 rayleigh_scatter_unused;   // xyz=β_R
    glm::vec4 mie_scatter_pad;           // x=β_M
    glm::vec4 sun_dir_intensity;         // xyz=sun_dir, w=intensity
};

static_assert(sizeof(vs_params_t) == 128);
static_assert(sizeof(fs_params_t) == 128);
static_assert(sizeof(atm_params_t) == 80);

} // namespace

// ---------------------------------------------------------------------------
// Pipeline setup
// ---------------------------------------------------------------------------

bool SokolRenderer::setup_pipelines() {
    const sg_shader_desc *shader_desc =
        voxov_scene_voxov_scene_shader_desc(sg_query_backend());
    if (shader_desc == nullptr) {
        slog_func("voxov", 2, 0,
                  "SokolRenderer: no compiled shader for active backend",
                  __LINE__, __FILE__, nullptr);
        return false;
    }
    pipelines_.scene_shader = sg_make_shader(shader_desc);
    if (pipelines_.scene_shader.id == SG_INVALID_ID) {
        slog_func("voxov", 1, 0, "SokolRenderer: failed to create scene shader",
                  __LINE__, __FILE__, nullptr);
        return false;
    }

    // Opaque pipeline
    sg_pipeline_desc opq_desc = {};
    opq_desc.shader = pipelines_.scene_shader;
    opq_desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
    opq_desc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT3;
    opq_desc.layout.attrs[2].format = SG_VERTEXFORMAT_FLOAT3;
    opq_desc.layout.attrs[3].format = SG_VERTEXFORMAT_FLOAT3;
    opq_desc.index_type = SG_INDEXTYPE_UINT32;
    opq_desc.cull_mode = SG_CULLMODE_BACK;
    opq_desc.face_winding = SG_FACEWINDING_CCW;
    opq_desc.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
    opq_desc.depth.write_enabled = true;
    opq_desc.label = "voxov-opaque";
    pipelines_.opaque = sg_make_pipeline(&opq_desc);
    opq_desc.index_type = SG_INDEXTYPE_UINT16;
    opq_desc.label = "voxov-opaque-u16";
    pipelines_.opaque_u16 = sg_make_pipeline(&opq_desc);

    // Wireframe pipeline (line list, no cull, depth-less-equal)
    sg_pipeline_desc wire_desc = {};
    wire_desc.shader = pipelines_.scene_shader;
    wire_desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
    wire_desc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT3;
    wire_desc.layout.attrs[2].format = SG_VERTEXFORMAT_FLOAT3;
    wire_desc.layout.attrs[3].format = SG_VERTEXFORMAT_FLOAT3;
    wire_desc.index_type = SG_INDEXTYPE_UINT32;
    wire_desc.primitive_type = SG_PRIMITIVETYPE_LINES;
    wire_desc.cull_mode = SG_CULLMODE_NONE;
    wire_desc.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
    wire_desc.depth.write_enabled = true;
    wire_desc.label = "voxov-wireframe";
    pipelines_.wireframe = sg_make_pipeline(&wire_desc);
    wire_desc.index_type = SG_INDEXTYPE_UINT16;
    wire_desc.label = "voxov-wireframe-u16";
    pipelines_.wireframe_u16 = sg_make_pipeline(&wire_desc);

    // Debug no-cull pipeline
    sg_pipeline_desc dnc_desc = {};
    dnc_desc.shader = pipelines_.scene_shader;
    dnc_desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
    dnc_desc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT3;
    dnc_desc.layout.attrs[2].format = SG_VERTEXFORMAT_FLOAT3;
    dnc_desc.layout.attrs[3].format = SG_VERTEXFORMAT_FLOAT3;
    dnc_desc.index_type = SG_INDEXTYPE_UINT32;
    dnc_desc.cull_mode = SG_CULLMODE_NONE;
    dnc_desc.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
    dnc_desc.depth.write_enabled = true;
    dnc_desc.label = "voxov-debug-nocull";
    pipelines_.debug_no_cull = sg_make_pipeline(&dnc_desc);
    dnc_desc.index_type = SG_INDEXTYPE_UINT16;
    dnc_desc.label = "voxov-debug-nocull-u16";
    pipelines_.debug_no_cull_u16 = sg_make_pipeline(&dnc_desc);

    // Debug x-ray pipeline (depth always, no write)
    sg_pipeline_desc xray_desc = {};
    xray_desc.shader = pipelines_.scene_shader;
    xray_desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
    xray_desc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT3;
    xray_desc.layout.attrs[2].format = SG_VERTEXFORMAT_FLOAT3;
    xray_desc.layout.attrs[3].format = SG_VERTEXFORMAT_FLOAT3;
    xray_desc.index_type = SG_INDEXTYPE_UINT32;
    xray_desc.cull_mode = SG_CULLMODE_NONE;
    xray_desc.depth.compare = SG_COMPAREFUNC_ALWAYS;
    xray_desc.depth.write_enabled = false;
    xray_desc.label = "voxov-debug-xray";
    pipelines_.debug_xray = sg_make_pipeline(&xray_desc);
    xray_desc.index_type = SG_INDEXTYPE_UINT16;
    xray_desc.label = "voxov-debug-xray-u16";
    pipelines_.debug_xray_u16 = sg_make_pipeline(&xray_desc);

    // Screen-space pipeline (no depth, no cull)
    sg_pipeline_desc scr_desc = {};
    scr_desc.shader = pipelines_.scene_shader;
    scr_desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
    scr_desc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT3;
    scr_desc.layout.attrs[2].format = SG_VERTEXFORMAT_FLOAT3;
    scr_desc.layout.attrs[3].format = SG_VERTEXFORMAT_FLOAT3;
    scr_desc.index_type = SG_INDEXTYPE_UINT32;
    scr_desc.cull_mode = SG_CULLMODE_NONE;
    scr_desc.depth.compare = SG_COMPAREFUNC_ALWAYS;
    scr_desc.depth.write_enabled = false;
    scr_desc.label = "voxov-screen";
    pipelines_.screen = sg_make_pipeline(&scr_desc);
    scr_desc.index_type = SG_INDEXTYPE_UINT16;
    scr_desc.label = "voxov-screen-u16";
    pipelines_.screen_u16 = sg_make_pipeline(&scr_desc);

    return true;
}

// ---------------------------------------------------------------------------
// Init / Shutdown
// ---------------------------------------------------------------------------

bool SokolRenderer::init(const RenderDeviceDesc &desc) {
    sg_desc sgdesc = {};
#if defined(VOXOV_PLATFORM_ANDROID)
    sgdesc.environment.defaults.color_format =
        desc.color_format != 0
            ? static_cast<sg_pixel_format>(desc.color_format)
            : SG_PIXELFORMAT_RGBA8;
    sgdesc.environment.defaults.depth_format =
        desc.depth_format != 0
            ? static_cast<sg_pixel_format>(desc.depth_format)
            : SG_PIXELFORMAT_DEPTH_STENCIL;
    sgdesc.environment.defaults.sample_count =
        desc.sample_count > 0 ? desc.sample_count : 1;
#else
    (void)desc;
    sgdesc.environment = sglue_environment();
#endif
    sgdesc.buffer_pool_size = 4096;
    sgdesc.logger.func = slog_func;
    sg_setup(&sgdesc);
    if (!sg_isvalid()) {
        slog_func("voxov", 1, 0, "SokolRenderer: sg_setup failed",
                  __LINE__, __FILE__, nullptr);
        return false;
    }

    sg_image_desc voxel_image_desc{};
    voxel_image_desc.type = SG_IMAGETYPE_ARRAY;
    voxel_image_desc.width = voxov::voxel_textures::kWidth;
    voxel_image_desc.height = voxov::voxel_textures::kHeight;
    voxel_image_desc.num_slices = voxov::voxel_textures::kLayerCount;
    voxel_image_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    voxel_image_desc.data.mip_levels[0] =
        SG_RANGE(voxov::voxel_textures::kRgba);
    voxel_image_desc.label = "voxov-voxel-materials";
    voxel_texture_ = sg_make_image(&voxel_image_desc);

    sg_view_desc voxel_view_desc{};
    voxel_view_desc.texture.image = voxel_texture_;
    voxel_view_desc.label = "voxov-voxel-materials-view";
    voxel_texture_view_ = sg_make_view(&voxel_view_desc);

    sg_sampler_desc voxel_sampler_desc{};
    voxel_sampler_desc.min_filter = SG_FILTER_NEAREST;
    voxel_sampler_desc.mag_filter = SG_FILTER_NEAREST;
    voxel_sampler_desc.wrap_u = SG_WRAP_REPEAT;
    voxel_sampler_desc.wrap_v = SG_WRAP_REPEAT;
    voxel_sampler_desc.label = "voxov-voxel-materials-sampler";
    voxel_sampler_ = sg_make_sampler(&voxel_sampler_desc);

    if (!setup_pipelines()) {
        sg_shutdown();
        return false;
    }

    // Default pass action
    pass_action_ = {};
    pass_action_.colors[0].load_action = SG_LOADACTION_CLEAR;
    pass_action_.colors[0].store_action = SG_STOREACTION_STORE;
    pass_action_.colors[0].clear_value = { 0.08f, 0.10f, 0.14f, 1.0f };
    pass_action_.depth.load_action = SG_LOADACTION_CLEAR;
    pass_action_.depth.store_action = SG_STOREACTION_DONTCARE;
    pass_action_.depth.clear_value = 1.0f;

    return true;
}

void SokolRenderer::shutdown() {
    destroy_mesh(transient_mesh_);
    destroy_mesh(wireframe_mesh_);
    destroy_mesh(debug_world_mesh_);
    destroy_mesh(debug_screen_mesh_);
    for (auto &[id, mesh] : cached_meshes_) {
        destroy_mesh(mesh);
    }
    cached_meshes_.clear();

    if (voxel_sampler_.id) sg_destroy_sampler(voxel_sampler_);
    if (voxel_texture_view_.id) sg_destroy_view(voxel_texture_view_);
    if (voxel_texture_.id) sg_destroy_image(voxel_texture_);
    voxel_sampler_ = {};
    voxel_texture_view_ = {};
    voxel_texture_ = {};

    if (pipelines_.scene_shader.id) sg_destroy_shader(pipelines_.scene_shader);
    if (pipelines_.opaque.id) sg_destroy_pipeline(pipelines_.opaque);
    if (pipelines_.opaque_u16.id) sg_destroy_pipeline(pipelines_.opaque_u16);
    if (pipelines_.wireframe.id) sg_destroy_pipeline(pipelines_.wireframe);
    if (pipelines_.wireframe_u16.id) sg_destroy_pipeline(pipelines_.wireframe_u16);
    if (pipelines_.debug_no_cull.id) sg_destroy_pipeline(pipelines_.debug_no_cull);
    if (pipelines_.debug_no_cull_u16.id) sg_destroy_pipeline(pipelines_.debug_no_cull_u16);
    if (pipelines_.debug_xray.id) sg_destroy_pipeline(pipelines_.debug_xray);
    if (pipelines_.debug_xray_u16.id) sg_destroy_pipeline(pipelines_.debug_xray_u16);
    if (pipelines_.screen.id) sg_destroy_pipeline(pipelines_.screen);
    if (pipelines_.screen_u16.id) sg_destroy_pipeline(pipelines_.screen_u16);
    pipelines_ = {};

    sg_shutdown();
    has_dynamic_mesh_hash_ = false;
}

// ---------------------------------------------------------------------------
// Mesh upload / destroy
// ---------------------------------------------------------------------------

void SokolRenderer::destroy_mesh(SokolGpuMesh &mesh) {
    if (mesh.vertex_buffer.id) sg_destroy_buffer(mesh.vertex_buffer);
    if (mesh.index_buffer.id) sg_destroy_buffer(mesh.index_buffer);
    mesh = {};
}

void SokolRenderer::upload_mesh(SokolGpuMesh &dst, const RenderMesh &src,
                                bool stream) {
    const bool use_16_bit_indices = src.use_16_bit_indices;
    const size_t index_count =
        use_16_bit_indices ? src.indices16.size() : src.indices.size();
    if (src.vertices.empty() || index_count == 0) {
        destroy_mesh(dst);
        return;
    }

    // Convert RenderVertex → SokolRenderVertex
    std::vector<SokolRenderVertex> vertices;
    vertices.reserve(src.vertices.size());
    glm::vec3 bmin(std::numeric_limits<float>::max());
    glm::vec3 bmax(-std::numeric_limits<float>::max());
    for (const RenderVertex &v : src.vertices) {
        vertices.push_back({
            v.position.x, v.position.y, v.position.z,
            v.color.r, v.color.g, v.color.b,
            v.normal.x, v.normal.y, v.normal.z,
            v.texcoord.x, v.texcoord.y, v.texcoord.z,
        });
        bmin = glm::min(bmin, v.position);
        bmax = glm::max(bmax, v.position);
    }
    dst.bounds_min = bmin;
    dst.bounds_max = bmax;
    dst.world_origin = src.world_origin;
    dst.material = src.material;
    dst.double_sided = src.double_sided;
    dst.content_hash = src.content_hash;
    dst.index_type =
        use_16_bit_indices ? SG_INDEXTYPE_UINT16 : SG_INDEXTYPE_UINT32;

    const sg_range vbuf_range{
        vertices.data(),
        vertices.size() * sizeof(vertices[0]),
    };
    const void *index_data =
        use_16_bit_indices ? static_cast<const void *>(src.indices16.data())
                           : static_cast<const void *>(src.indices.data());
    const size_t index_size =
        use_16_bit_indices ? sizeof(src.indices16[0]) : sizeof(src.indices[0]);
    const sg_range ibuf_range{
        index_data,
        index_count * index_size,
    };

    if (stream) {
        // Dynamic: create or recreate if size grew.
        const size_t new_vb_size = vbuf_range.size;
        const size_t new_ib_size = ibuf_range.size;
        if (dst.vertex_buffer.id == 0 ||
            dst.vertex_buffer_size < new_vb_size) {
            if (dst.vertex_buffer.id) sg_destroy_buffer(dst.vertex_buffer);
            sg_buffer_desc dvb_desc = {};
            dvb_desc.usage.vertex_buffer = true;
            dvb_desc.usage.stream_update = true;
            dvb_desc.size = new_vb_size;
            dvb_desc.label = "voxov-dynamic-vbuf";
            dst.vertex_buffer = sg_make_buffer(&dvb_desc);
            dst.vertex_buffer_size = new_vb_size;
        }
        sg_update_buffer(dst.vertex_buffer, &vbuf_range);

        if (dst.index_buffer.id == 0 ||
            dst.index_buffer_size < new_ib_size) {
            if (dst.index_buffer.id) sg_destroy_buffer(dst.index_buffer);
            sg_buffer_desc dib_desc = {};
            dib_desc.usage.index_buffer = true;
            dib_desc.usage.stream_update = true;
            dib_desc.size = new_ib_size;
            dib_desc.label = "voxov-dynamic-ibuf";
            dst.index_buffer = sg_make_buffer(&dib_desc);
            dst.index_buffer_size = new_ib_size;
        }
        sg_update_buffer(dst.index_buffer, &ibuf_range);
    } else {
        // Static: destroy old, create new immutable buffers.
        if (dst.vertex_buffer.id) sg_destroy_buffer(dst.vertex_buffer);
        if (dst.index_buffer.id) sg_destroy_buffer(dst.index_buffer);

        sg_buffer_desc svb_desc = {};
        svb_desc.usage.vertex_buffer = true;
        svb_desc.data = vbuf_range;
        svb_desc.label = "voxov-static-vbuf";
        dst.vertex_buffer = sg_make_buffer(&svb_desc);
        dst.vertex_buffer_size = vbuf_range.size;
        sg_buffer_desc sib_desc = {};
        sib_desc.usage.index_buffer = true;
        sib_desc.data = ibuf_range;
        sib_desc.label = "voxov-static-ibuf";
        dst.index_buffer = sg_make_buffer(&sib_desc);
        dst.index_buffer_size = ibuf_range.size;
    }

    dst.index_count = static_cast<uint32_t>(index_count);
}

// ---------------------------------------------------------------------------
// Draw
// ---------------------------------------------------------------------------

void SokolRenderer::draw_mesh(const SokolGpuMesh &mesh,
                              const glm::mat4 &mvp,
                              const glm::mat4 &model,
                              const glm::vec3 &camera_pos,
                              const glm::dvec3 &camera_relative_origin,
                              sg_pipeline pipeline_u32,
                              sg_pipeline pipeline_u16) {
    if (!mesh.vertex_buffer.id || !mesh.index_buffer.id ||
        mesh.index_count == 0) {
        return;
    }

    // Debug: log first 5 draw calls to confirm GPU draw path executes.
    // WHY: blocks invisible despite valid meshes in scene — need to verify
    // sg_draw() is actually called with valid buffer handles and pipeline.
    // TODO: remove once voxel rendering is confirmed working.
    static int draw_count = 0;
    if (draw_count < 5) {
        std::fprintf(stderr, "draw_mesh[%d]: idx_count=%d vb.id=%d ib.id=%d pipeline_u32.id=%d\n",
                     draw_count, mesh.index_count, mesh.vertex_buffer.id, mesh.index_buffer.id, pipeline_u32.id);
        draw_count++;
    }

    const sg_pipeline pipeline =
        mesh.index_type == SG_INDEXTYPE_UINT16 ? pipeline_u16 : pipeline_u32;
    if (pipeline.id != 0) {
        sg_apply_pipeline(pipeline);
    }
    const glm::vec3 relative_camera_pos =
        glm::vec3(glm::dvec3(camera_pos) - camera_relative_origin);
    const vs_params_t vs_params{ mvp, model };
    const fs_params_t fs_params{
        glm::vec4(light_.direction, 0.0f),
        glm::vec4(light_.ambient, 0.0f),
        glm::vec4(light_.diffuse, 0.0f),
        glm::vec4(light_.specular, 0.0f),
        glm::vec4(material_.ambient, 0.0f),
        glm::vec4(material_.diffuse, 0.0f),
        glm::vec4(material_.specular, material_.shininess),
        glm::vec4(relative_camera_pos, 0.0f),
    };
    const sg_range vs_range = SG_RANGE(vs_params);
    const sg_range fs_range = SG_RANGE(fs_params);
    atm_params_t mesh_atmosphere = atm_uniforms_;
    // Atmosphere positions live in the same local frame as this retained
    // mesh. planet_center_radius arrives relative to the current camera
    // origin, so translate it into the mesh's independently retained origin.
    const glm::dvec3 planet_center_world =
        glm::dvec3(atm_uniforms_.planet_center_radius);
    mesh_atmosphere.planet_center_radius = glm::vec4(
        glm::vec3(planet_center_world - mesh.world_origin),
        atm_uniforms_.planet_center_radius.w);
    const sg_range atm_range = SG_RANGE(mesh_atmosphere);
    sg_apply_uniforms(0, &vs_range);
    sg_apply_uniforms(1, &fs_range);
    sg_apply_uniforms(2, &atm_range);
    sg_bindings bind = {};
    bind.vertex_buffers[0] = mesh.vertex_buffer;
    bind.index_buffer = mesh.index_buffer;
    bind.views[0] = voxel_texture_view_;
    bind.samplers[0] = voxel_sampler_;
    sg_apply_bindings(&bind);
    sg_draw(0, static_cast<int>(mesh.index_count), 1);
}

void SokolRenderer::draw_wireframe(const SokolGpuMesh &mesh,
                                     const glm::mat4 &mvp,
                                     const glm::vec3 &camera_pos) {
    if (!mesh.vertex_buffer.id || !mesh.index_buffer.id ||
        mesh.index_count == 0) {
        return;
    }
    sg_pipeline pipeline = (mesh.index_type == SG_INDEXTYPE_UINT16)
                               ? pipelines_.wireframe_u16
                               : pipelines_.wireframe;
    if (pipeline.id != 0) {
        sg_apply_pipeline(pipeline);
    }
    const vs_params_t vs_params{ mvp, glm::mat4(1.0f) };
    const fs_params_t fs_params{
        glm::vec4(light_.direction, 0.0f),
        glm::vec4(light_.ambient, 0.0f),
        glm::vec4(light_.diffuse, 0.0f),
        glm::vec4(light_.specular, 0.0f),
        glm::vec4(1.0f, 1.0f, 1.0f, 0.0f), // material_ambient (white → vertex color)
        glm::vec4(1.0f, 1.0f, 1.0f, 0.0f), // material_diffuse (white → vertex color)
        glm::vec4(0.0f, 0.0f, 0.0f, 0.0f), // material_specular (no specular)
        glm::vec4(camera_pos, 0.0f),
    };
    const sg_range vs_range = SG_RANGE(vs_params);
    const sg_range fs_range = SG_RANGE(fs_params);
    const sg_range atm_range = SG_RANGE(atm_uniforms_);
    sg_apply_uniforms(0, &vs_range);
    sg_apply_uniforms(1, &fs_range);
    sg_apply_uniforms(2, &atm_range);
    sg_bindings bind = {};
    bind.vertex_buffers[0] = mesh.vertex_buffer;
    bind.index_buffer = mesh.index_buffer;
    bind.views[0] = voxel_texture_view_;
    bind.samplers[0] = voxel_sampler_;
    sg_apply_bindings(&bind);
    sg_draw(0, static_cast<int>(mesh.index_count), 1);
}

// ---------------------------------------------------------------------------
// Scene upload
// ---------------------------------------------------------------------------

void SokolRenderer::upload_scene(const RenderScene &new_scene) {
    // Evict stale cached meshes.
    std::unordered_map<uint64_t, bool> live_ids;
    for (const RenderMesh &mesh : new_scene.opaque_meshes) {
        if (mesh.mesh_id != 0) live_ids[mesh.mesh_id] = true;
    }
    std::vector<uint64_t> stale_ids;
    for (const auto &[id, _] : cached_meshes_) {
        if (live_ids.find(id) == live_ids.end()) stale_ids.push_back(id);
    }
    for (uint64_t id : stale_ids) {
        destroy_mesh(cached_meshes_[id]);
        cached_meshes_.erase(id);
    }

    // Build transient mesh from meshes with mesh_id == 0.
    RenderMesh transient_scene{};
    bool transient_origin_set = false;
    auto append_mesh = [&](const RenderMesh &mesh) {
        if (!transient_origin_set) {
            transient_scene.world_origin = mesh.world_origin;
            transient_origin_set = true;
        }
        const uint32_t base = static_cast<uint32_t>(transient_scene.vertices.size());
        const glm::dvec3 origin_delta =
            mesh.world_origin - transient_scene.world_origin;
        for (const RenderVertex &vertex : mesh.vertices) {
            RenderVertex converted = vertex;
            converted.position = glm::vec3(
                glm::dvec3(vertex.position) + origin_delta);
            transient_scene.vertices.push_back(converted);
        }
        if (mesh.use_16_bit_indices) {
            for (uint16_t idx : mesh.indices16) {
                transient_scene.indices.push_back(base + idx);
            }
        } else {
            for (uint32_t idx : mesh.indices) {
                transient_scene.indices.push_back(base + idx);
            }
        }
    };
    for (const RenderMesh &mesh : new_scene.opaque_meshes) {
        if (mesh.mesh_id == 0) {
            append_mesh(mesh);
            continue;
        }
        const auto it = cached_meshes_.find(mesh.mesh_id);
        const uint32_t mesh_index_count = mesh.use_16_bit_indices
            ? static_cast<uint32_t>(mesh.indices16.size())
            : static_cast<uint32_t>(mesh.indices.size());
        if (it != cached_meshes_.end()) {
            // Remesh when content changes, even if topology counts are stable.
            // Camera-relative origin shifts and equal-sized voxel edits are
            // otherwise left pointing at stale GPU positions.
            if (it->second.index_count == mesh_index_count &&
                it->second.vertex_buffer_size ==
                    mesh.vertices.size() * sizeof(SokolRenderVertex) &&
                mesh.content_hash != 0 &&
                it->second.content_hash == mesh.content_hash) {
                it->second.world_origin = mesh.world_origin;
                it->second.double_sided = mesh.double_sided;
                continue;
            }
            destroy_mesh(it->second);
            cached_meshes_.erase(it);
        }
        SokolGpuMesh uploaded{};
        upload_mesh(uploaded, mesh, false);
        cached_meshes_[mesh.mesh_id] = uploaded;
    }

    // ── Wireframe mesh upload ─────────────────────────────────────────
    // Hash the set of mesh_ids to detect changes. Wireframe planet uses
    // a stable mesh_id (0x574952454652414d) and never changes after init.
    // Skipping the re-upload avoids stalling the GPU command stream with
    // 2.8 MB of vertex data every frame (was the cause of 1 FPS at planet scale).
    uint64_t wireframe_hash = 0;
    for (const RenderMesh &mesh : new_scene.wireframe_meshes) {
        wireframe_hash ^= mesh.mesh_id + 0x9e3779b97f4a7c15ull +
                          (wireframe_hash << 6) + (wireframe_hash >> 2);
    }
    if (wireframe_hash != last_wireframe_hash_ || wireframe_hash == 0) {
        RenderMesh wireframe_scene{};
        for (const RenderMesh &mesh : new_scene.wireframe_meshes) {
            const uint32_t base = static_cast<uint32_t>(wireframe_scene.vertices.size());
            wireframe_scene.vertices.insert(wireframe_scene.vertices.end(),
                                             mesh.vertices.begin(), mesh.vertices.end());
            if (mesh.use_16_bit_indices) {
                for (uint16_t idx : mesh.indices16) {
                    wireframe_scene.indices.push_back(base + idx);
                }
            } else {
                for (uint32_t idx : mesh.indices) {
                    wireframe_scene.indices.push_back(base + idx);
                }
            }
        }
        upload_mesh(wireframe_mesh_, wireframe_scene, false);
        last_wireframe_hash_ = wireframe_hash;
    }

    upload_mesh(transient_mesh_, transient_scene, true);
    upload_mesh(debug_world_mesh_, new_scene.debug_world, false);
    upload_mesh(debug_screen_mesh_, new_scene.debug_screen, false);
    last_debug_world_hash_ = mesh_size_token(new_scene.debug_world);
    last_debug_screen_hash_ = mesh_size_token(new_scene.debug_screen);
    has_dynamic_mesh_hash_ = true;
}

void SokolRenderer::update_dynamic_meshes(const RenderMesh &debug_world,
                                          const RenderMesh &debug_screen) {
    const uint64_t world_hash = mesh_size_token(debug_world);
    const uint64_t screen_hash = mesh_size_token(debug_screen);
    if (has_dynamic_mesh_hash_ &&
        world_hash == last_debug_world_hash_ &&
        screen_hash == last_debug_screen_hash_) {
        return;
    }

    upload_mesh(debug_world_mesh_, debug_world, false);
    upload_mesh(debug_screen_mesh_, debug_screen, false);
    last_debug_world_hash_ = world_hash;
    last_debug_screen_hash_ = screen_hash;
    has_dynamic_mesh_hash_ = true;
}

// ---------------------------------------------------------------------------
// Frame rendering
// ---------------------------------------------------------------------------

void SokolRenderer::render_frame(const RenderFrameContext &ctx,
                                  RenderStats &stats,
                                  const RenderSurface &surface) {
    // Reset per-frame GPU counters.
    uint32_t draw_calls = 0;

    // ── Copy atmosphere uniforms from frame context ───────────────────
    // Retained meshes can have different local origins. Keep the atmosphere
    // centre absolute here and translate it into each mesh frame at draw time.
    atm_uniforms_.planet_center_radius = glm::vec4(
        glm::vec3(ctx.camera_origin.world_origin) +
            glm::vec3(ctx.atmosphere.planet_center_radius),
        ctx.atmosphere.planet_center_radius.w);
    atm_uniforms_.atm_params_1 = ctx.atmosphere.atm_params_1;
    atm_uniforms_.rayleigh_scatter = ctx.atmosphere.rayleigh_scatter;
    atm_uniforms_.mie_scatter = ctx.atmosphere.mie_scatter;
    atm_uniforms_.sun_dir_intensity = ctx.atmosphere.sun_dir_intensity;

    // ── Set sky color as clear color (tone mapped from HDR) ──────────
    pass_action_.colors[0].clear_value = {
        ctx.atmosphere.sky_color.r,
        ctx.atmosphere.sky_color.g,
        ctx.atmosphere.sky_color.b,
        1.0f
    };

    sg_pass pass = {};
    pass.action = pass_action_;
#if defined(VOXOV_PLATFORM_ANDROID)
    pass.swapchain.width = surface.width;
    pass.swapchain.height = surface.height;
    pass.swapchain.sample_count = 1;
    pass.swapchain.color_format = SG_PIXELFORMAT_RGBA8;
    pass.swapchain.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;
    pass.swapchain.gl.framebuffer = 0;
#else
    pass.swapchain = sglue_swapchain();
#endif
    sg_begin_pass(&pass);

    const uint32_t view_count = std::max(1u, std::min(ctx.view_count, 2u));
    for (uint32_t i = 0; i < view_count; ++i) {
        const RenderView &view = ctx.views[i];
        const int vx = static_cast<int>(view.viewport.x * static_cast<float>(surface.width));
        const int vy = static_cast<int>(view.viewport.y * static_cast<float>(surface.height));
        const int vw = std::max(1, static_cast<int>(view.viewport.z * static_cast<float>(surface.width)));
        const int vh = std::max(1, static_cast<int>(view.viewport.w * static_cast<float>(surface.height)));

        sg_apply_viewport(vx, vy, vw, vh, true);
        const glm::mat4 p = view.camera.projection(static_cast<float>(vw) / static_cast<float>(vh));
        const glm::mat4 vp = p * view.camera.view();
        const glm::mat4 model = glm::mat4(1.0f);
        const glm::vec3 camera_pos = view.camera.transform.position;
        auto mesh_vp = [&](const SokolGpuMesh &mesh) {
            if (view.camera.has_view_override()) {
                return vp * glm::translate(
                    glm::mat4(1.0f), glm::vec3(mesh.world_origin));
            }
            Camera relative_camera = view.camera;
            relative_camera.transform.position = glm::vec3(
                glm::dvec3(camera_pos) - mesh.world_origin);
            return p * relative_camera.view();
        };

        // ── Draw call counting ──────────────────────────────────────────
        // WHY: count every sg_draw issued per frame for the dev HUD
        // (F2). Helps identify whether GPU is draw-call bound.
        auto record_draw = [&](const SokolGpuMesh &mesh) {
            if (mesh.index_count > 0) {
                draw_calls++;
            }
        };

        // Opaque geometry (planet block meshes are relative to camera_origin)
        record_draw(transient_mesh_);
        const glm::mat4 transient_vp = mesh_vp(transient_mesh_);
        draw_mesh(transient_mesh_, transient_vp, model, camera_pos,
                  transient_mesh_.world_origin,
                  pipelines_.opaque, pipelines_.opaque_u16);

        // Each retained mesh carries its own double-precision origin, so a
        // camera rebase changes only the draw transform, never its GPU data.
        for (const auto &[id, mesh] : cached_meshes_) {
            const glm::mat4 retained_vp = mesh_vp(mesh);
            const Frustum retained_frustum = extract_frustum(retained_vp);
            if (!aabb_in_frustum(retained_frustum, mesh.bounds_min,
                                 mesh.bounds_max)) {
                continue;
            }
            record_draw(mesh);
            const sg_pipeline pipeline_u32 =
                mesh.double_sided ? pipelines_.debug_no_cull : pipelines_.opaque;
            const sg_pipeline pipeline_u16 = mesh.double_sided
                ? pipelines_.debug_no_cull_u16
                : pipelines_.opaque_u16;
            draw_mesh(mesh, retained_vp, model, camera_pos,
                      mesh.world_origin,
                      pipeline_u32, pipeline_u16);
        }

        // Wireframe geometry (drawn over opaque, with depth).
        // Wireframe verts are absolute world (see build_wireframe_... and
        // its mesh_id caching); must use raw vp.
        record_draw(wireframe_mesh_);
        draw_wireframe(wireframe_mesh_, vp, camera_pos);

        // Debug world (x-ray or normal)
        record_draw(debug_world_mesh_);
        draw_mesh(debug_world_mesh_, vp, model, camera_pos,
                  debug_world_mesh_.world_origin,
                  ctx.debug_xray ? pipelines_.debug_xray : pipelines_.opaque,
                  ctx.debug_xray ? pipelines_.debug_xray_u16
                                 : pipelines_.opaque_u16);

        // Screen-space overlay
        record_draw(debug_screen_mesh_);
        draw_mesh(debug_screen_mesh_, glm::mat4(1.0f), model, camera_pos,
                  glm::dvec3(0.0), pipelines_.screen, pipelines_.screen_u16);
    }

    // Write GPU draw call count back so the dev HUD can display it.
    stats.draw_call_count = draw_calls;

    sg_end_pass();
    sg_commit();
}

// ---------------------------------------------------------------------------
// Screenshot capture — reads framebuffer via glReadPixels, flips vertically,
// and writes PNG via stb_image_write.
// ---------------------------------------------------------------------------

bool SokolRenderer::capture_screenshot(const char *filepath,
                                       int width, int height) {
#if !defined(SOKOL_GLCORE) && !defined(SOKOL_GLES3)
    (void)filepath;
    (void)width;
    (void)height;
    return false;
#else
    if (width <= 0 || height <= 0) return false;

    // Allocate buffer for RGBA pixels
    const size_t row_bytes = static_cast<size_t>(width) * 4;
    std::vector<uint8_t> pixels(row_bytes * static_cast<size_t>(height));

    // Read from the default framebuffer (sokol renders to FBO 0)
    sg_commit(); // flush GPU commands
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    // OpenGL has origin at bottom-left; PNG expects top-left.
    // Flip the rows in-place.
    std::vector<uint8_t> flipped(row_bytes * static_cast<size_t>(height));
    for (int y = 0; y < height; ++y) {
        const size_t src_row = static_cast<size_t>(height - 1 - y) * row_bytes;
        const size_t dst_row = static_cast<size_t>(y) * row_bytes;
        std::memcpy(flipped.data() + dst_row,
                    pixels.data() + src_row,
                    row_bytes);
    }

    const int result = stbi_write_png(filepath, width, height, 4,
                                      flipped.data(),
                                      static_cast<int>(row_bytes));
    return result != 0;
#endif
}
