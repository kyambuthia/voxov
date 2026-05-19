#include "engine_render/sokol_renderer.hpp"

#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "sokol_log.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>
#include <vector>

#include <glm/gtc/type_ptr.hpp>

// ---------------------------------------------------------------------------
// Shader — inline GLSL for Phase 1 smoke-test.  Production path is
// sokol-shdc compiled shaders (see CMake rule for voxov_scene.glsl).
// ---------------------------------------------------------------------------

#if defined(SOKOL_GLCORE)
static const char *kSceneVsSrc = R"(
    #version 330
    uniform mat4 mvp;
    layout(location=0) in vec3 position;
    layout(location=1) in vec3 color0;
    layout(location=2) in vec3 normal;
    out vec3 v_color;
    out vec3 v_normal;
    void main() {
        v_color = color0;
        v_normal = normal;
        gl_Position = mvp * vec4(position, 1.0);
    }
)";
static const char *kSceneFsSrc = R"(
    #version 330
    in vec3 v_color;
    in vec3 v_normal;
    out vec4 frag_color;
    void main() {
        float normal_len2 = dot(v_normal, v_normal);
        if (normal_len2 < 0.001) {
            frag_color = vec4(v_color, 1.0);
            return;
        }
        vec3 n = normalize(v_normal);
        float ndl = clamp(dot(n, normalize(vec3(0.3, 0.8, 0.4))), 0.0, 1.0);
        float stepped = floor(ndl * 4.0) / 4.0;
        float rim = pow(1.0 - max(dot(n, vec3(0.0, 0.0, 1.0)), 0.0), 2.0);
        vec3 base = v_color * (0.5 + 0.5 * stepped);
        frag_color = vec4(base + rim * 0.15, 1.0);
    }
)";
#elif defined(SOKOL_GLES3)
static const char *kSceneVsSrc = R"(
    #version 300 es
    uniform mat4 mvp;
    layout(location=0) in vec3 position;
    layout(location=1) in vec3 color0;
    layout(location=2) in vec3 normal;
    out vec3 v_color;
    out vec3 v_normal;
    void main() {
        v_color = color0;
        v_normal = normal;
        gl_Position = mvp * vec4(position, 1.0);
    }
)";
static const char *kSceneFsSrc = R"(
    #version 300 es
    precision mediump float;
    in vec3 v_color;
    in vec3 v_normal;
    out vec4 frag_color;
    void main() {
        float normal_len2 = dot(v_normal, v_normal);
        if (normal_len2 < 0.001) {
            frag_color = vec4(v_color, 1.0);
            return;
        }
        vec3 n = normalize(v_normal);
        float ndl = clamp(dot(n, normalize(vec3(0.3, 0.8, 0.4))), 0.0, 1.0);
        float stepped = floor(ndl * 4.0) / 4.0;
        float rim = pow(1.0 - max(dot(n, vec3(0.0, 0.0, 1.0)), 0.0), 2.0);
        vec3 base = v_color * (0.5 + 0.5 * stepped);
        frag_color = vec4(base + rim * 0.15, 1.0);
    }
)";
#else
// Dummy fallback for non-GL backends (Metal/D3D11/WGPU) — production uses shdc.
static const char *kSceneVsSrc = nullptr;
static const char *kSceneFsSrc = nullptr;
#endif

namespace {

// O(1) change-detection token: vertex count + index count.
uint64_t mesh_size_token(const RenderMesh &mesh) {
    return (static_cast<uint64_t>(mesh.vertices.size()) << 32) |
           static_cast<uint64_t>(mesh.indices.size());
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
};

} // namespace

// ---------------------------------------------------------------------------
// Pipeline setup
// ---------------------------------------------------------------------------

bool SokolRenderer::setup_pipelines() {
    if (!kSceneVsSrc || !kSceneFsSrc) {
        // Non-GL backend detected — sokol-shdc compiled shaders are required.
        // For Phase 1 smoke-test on Linux with GLCORE, inline GLSL works.
        slog_func("voxov", 2, 0,
                  "SokolRenderer: inline GLSL only supported on GL backends",
                  __LINE__, __FILE__, nullptr);
        return false;
    }

    sg_shader_desc shd_desc = {};
    shd_desc.vertex_func.source = kSceneVsSrc;
    shd_desc.fragment_func.source = kSceneFsSrc;

    // Uniform block: one mat4 mvp at binding 0.
    shd_desc.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    shd_desc.uniform_blocks[0].size = sizeof(vs_params_t);
    shd_desc.uniform_blocks[0].layout = SG_UNIFORMLAYOUT_NATIVE;
    shd_desc.uniform_blocks[0].glsl_uniforms[0].glsl_name = "mvp";
    shd_desc.uniform_blocks[0].glsl_uniforms[0].type = SG_UNIFORMTYPE_MAT4;
    shd_desc.uniform_blocks[0].glsl_uniforms[0].array_count = 1;

    // Vertex attributes: position(0) float3, color0(1) float3, normal(2) float3.
    shd_desc.attrs[0].glsl_name = "position";
    shd_desc.attrs[1].glsl_name = "color0";
    shd_desc.attrs[2].glsl_name = "normal";

    pipelines_.scene_shader = sg_make_shader(&shd_desc);
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
    opq_desc.index_type = SG_INDEXTYPE_UINT32;
    opq_desc.cull_mode = SG_CULLMODE_BACK;
    opq_desc.face_winding = SG_FACEWINDING_CCW;
    opq_desc.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
    opq_desc.depth.write_enabled = true;
    opq_desc.label = "voxov-opaque";
    pipelines_.opaque = sg_make_pipeline(&opq_desc);

    // Debug no-cull pipeline
    sg_pipeline_desc dnc_desc = {};
    dnc_desc.shader = pipelines_.scene_shader;
    dnc_desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
    dnc_desc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT3;
    dnc_desc.layout.attrs[2].format = SG_VERTEXFORMAT_FLOAT3;
    dnc_desc.index_type = SG_INDEXTYPE_UINT32;
    dnc_desc.cull_mode = SG_CULLMODE_NONE;
    dnc_desc.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
    dnc_desc.depth.write_enabled = true;
    dnc_desc.label = "voxov-debug-nocull";
    pipelines_.debug_no_cull = sg_make_pipeline(&dnc_desc);

    // Debug x-ray pipeline (depth always, no write)
    sg_pipeline_desc xray_desc = {};
    xray_desc.shader = pipelines_.scene_shader;
    xray_desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
    xray_desc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT3;
    xray_desc.layout.attrs[2].format = SG_VERTEXFORMAT_FLOAT3;
    xray_desc.index_type = SG_INDEXTYPE_UINT32;
    xray_desc.cull_mode = SG_CULLMODE_NONE;
    xray_desc.depth.compare = SG_COMPAREFUNC_ALWAYS;
    xray_desc.depth.write_enabled = false;
    xray_desc.label = "voxov-debug-xray";
    pipelines_.debug_xray = sg_make_pipeline(&xray_desc);

    // Screen-space pipeline (no depth, no cull)
    sg_pipeline_desc scr_desc = {};
    scr_desc.shader = pipelines_.scene_shader;
    scr_desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
    scr_desc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT3;
    scr_desc.layout.attrs[2].format = SG_VERTEXFORMAT_FLOAT3;
    scr_desc.index_type = SG_INDEXTYPE_UINT32;
    scr_desc.cull_mode = SG_CULLMODE_NONE;
    scr_desc.depth.compare = SG_COMPAREFUNC_ALWAYS;
    scr_desc.depth.write_enabled = false;
    scr_desc.label = "voxov-screen";
    pipelines_.screen = sg_make_pipeline(&scr_desc);

    return true;
}

// ---------------------------------------------------------------------------
// Init / Shutdown
// ---------------------------------------------------------------------------

bool SokolRenderer::init(const RenderDeviceDesc &desc) {
    sg_desc sgdesc = {};
    sgdesc.environment = sglue_environment();
    sgdesc.logger.func = slog_func;
    sg_setup(&sgdesc);
    if (!sg_isvalid()) {
        slog_func("voxov", 1, 0, "SokolRenderer: sg_setup failed",
                  __LINE__, __FILE__, nullptr);
        return false;
    }

    if (!setup_pipelines()) {
        sg_shutdown();
        return false;
    }

    // Default pass action
    pass_action_ = (sg_pass_action){
        .colors = {
            [0] = {
                .load_action = SG_LOADACTION_CLEAR,
                .store_action = SG_STOREACTION_STORE,
                .clear_value = { 0.08f, 0.10f, 0.14f, 1.0f },
            },
        },
        .depth = {
            .load_action = SG_LOADACTION_CLEAR,
            .store_action = SG_STOREACTION_DONTCARE,
            .clear_value = 1.0f,
        },
    };

    // --- Debug triangle (screen-space, always visible in center) ---
    {
        RenderMesh tri;
        tri.vertices = {
            {glm::vec3(-0.8f, -0.6f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f)},
            {glm::vec3( 0.8f, -0.6f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f)},
            {glm::vec3( 0.0f,  0.7f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f)},
        };
        tri.indices = {0, 1, 2};
        upload_mesh(debug_triangle_mesh_, tri, false);
        std::fprintf(stderr, "triangle init: vbuf.id=%d ibuf.id=%d idx_count=%u\n",
            debug_triangle_mesh_.vertex_buffer.id,
            debug_triangle_mesh_.index_buffer.id,
            debug_triangle_mesh_.index_count);
    }

    return true;
}

void SokolRenderer::shutdown() {
    destroy_mesh(transient_mesh_);
    destroy_mesh(debug_grid_mesh_);
    destroy_mesh(debug_world_mesh_);
    destroy_mesh(debug_screen_mesh_);
    destroy_mesh(debug_triangle_mesh_);
    for (auto &[id, mesh] : cached_meshes_) {
        destroy_mesh(mesh);
    }
    cached_meshes_.clear();

    if (pipelines_.scene_shader.id) sg_destroy_shader(pipelines_.scene_shader);
    if (pipelines_.opaque.id) sg_destroy_pipeline(pipelines_.opaque);
    if (pipelines_.debug_no_cull.id) sg_destroy_pipeline(pipelines_.debug_no_cull);
    if (pipelines_.debug_xray.id) sg_destroy_pipeline(pipelines_.debug_xray);
    if (pipelines_.screen.id) sg_destroy_pipeline(pipelines_.screen);
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
    if (src.vertices.empty() || src.indices.empty()) {
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
        });
        bmin = glm::min(bmin, v.position);
        bmax = glm::max(bmax, v.position);
    }
    dst.bounds_min = bmin;
    dst.bounds_max = bmax;
    dst.material = src.material;

    const sg_range vbuf_range = {
        .ptr = vertices.data(),
        .size = vertices.size() * sizeof(vertices[0]),
    };
    const sg_range ibuf_range = {
        .ptr = src.indices.data(),
        .size = src.indices.size() * sizeof(src.indices[0]),
    };

    if (stream) {
        // Dynamic: create with stream_update usage if new, otherwise update.
        if (dst.vertex_buffer.id == 0) {
            sg_buffer_desc dvb_desc = {};
            dvb_desc.usage = { .vertex_buffer = true, .stream_update = true };
            dvb_desc.size = vbuf_range.size;
            dvb_desc.label = "voxov-dynamic-vbuf";
            dst.vertex_buffer = sg_make_buffer(&dvb_desc);
        }
        sg_update_buffer(dst.vertex_buffer, &vbuf_range);

        if (dst.index_buffer.id == 0) {
            sg_buffer_desc dib_desc = {};
            dib_desc.usage = { .index_buffer = true, .stream_update = true };
            dib_desc.size = ibuf_range.size;
            dib_desc.label = "voxov-dynamic-ibuf";
            dst.index_buffer = sg_make_buffer(&dib_desc);
        }
        sg_update_buffer(dst.index_buffer, &ibuf_range);
    } else {
        // Static: destroy old, create new immutable buffers.
        if (dst.vertex_buffer.id) sg_destroy_buffer(dst.vertex_buffer);
        if (dst.index_buffer.id) sg_destroy_buffer(dst.index_buffer);

        sg_buffer_desc svb_desc = {};
        svb_desc.usage = { .vertex_buffer = true };
        svb_desc.data = vbuf_range;
        svb_desc.label = "voxov-static-vbuf";
        dst.vertex_buffer = sg_make_buffer(&svb_desc);
        sg_buffer_desc sib_desc = {};
        sib_desc.usage = { .index_buffer = true };
        sib_desc.data = ibuf_range;
        sib_desc.label = "voxov-static-ibuf";
        dst.index_buffer = sg_make_buffer(&sib_desc);
    }

    dst.index_count = static_cast<uint32_t>(src.indices.size());
}

// ---------------------------------------------------------------------------
// Draw
// ---------------------------------------------------------------------------

void SokolRenderer::draw_mesh(const SokolGpuMesh &mesh, const glm::mat4 &mvp) {
    if (!mesh.vertex_buffer.id || !mesh.index_buffer.id || mesh.index_count == 0) {
        return;
    }

    const vs_params_t vs_params = { .mvp = mvp };
    const sg_range vs_range = SG_RANGE(vs_params);
    sg_apply_uniforms(0, &vs_range);
    sg_bindings bind = {};
    bind.vertex_buffers[0] = mesh.vertex_buffer;
    bind.index_buffer = mesh.index_buffer;
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
    auto append_mesh = [&](const RenderMesh &mesh) {
        const uint32_t base = static_cast<uint32_t>(transient_scene.vertices.size());
        transient_scene.vertices.insert(transient_scene.vertices.end(),
                                        mesh.vertices.begin(), mesh.vertices.end());
        for (uint32_t idx : mesh.indices) {
            transient_scene.indices.push_back(base + idx);
        }
    };
    for (const RenderMesh &mesh : new_scene.opaque_meshes) {
        if (mesh.mesh_id == 0) {
            append_mesh(mesh);
        } else if (cached_meshes_.find(mesh.mesh_id) == cached_meshes_.end()) {
            SokolGpuMesh uploaded{};
            upload_mesh(uploaded, mesh, false);
            cached_meshes_[mesh.mesh_id] = uploaded;
        }
    }

    upload_mesh(transient_mesh_, transient_scene, true);
    upload_mesh(debug_grid_mesh_, new_scene.debug_grid, false);
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
                                  const RenderStats &stats,
                                  const RenderSurface &surface) {
    (void)stats;
    static int frame_count = 0;
    if (frame_count < 2) {
        std::fprintf(stderr, "render_frame #%d: surface=%dx%d views=%u sc_ready=%d\n",
            frame_count, surface.width, surface.height, ctx.view_count,
            pipelines_.screen.id != SG_INVALID_ID ? 1 : 0);
        frame_count++;
    }
    sg_pass pass = {};
    pass.action = pass_action_;
    pass.swapchain = sglue_swapchain();
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

        // Opaque geometry
        sg_apply_pipeline(pipelines_.opaque);
        draw_mesh(transient_mesh_, vp);

        const Frustum frustum = extract_frustum(vp);
        for (const auto &[id, mesh] : cached_meshes_) {
            if (aabb_in_frustum(frustum, mesh.bounds_min, mesh.bounds_max)) {
                draw_mesh(mesh, vp);
            }
        }

        // Debug grid (no cull)
        sg_apply_pipeline(pipelines_.debug_no_cull);
        draw_mesh(debug_grid_mesh_, vp);

        // Debug world (x-ray or normal)
        sg_apply_pipeline(ctx.debug_xray ? pipelines_.debug_xray : pipelines_.opaque);
        draw_mesh(debug_world_mesh_, vp);

        // Screen-space overlay
        sg_apply_pipeline(pipelines_.screen);
        draw_mesh(debug_screen_mesh_, glm::mat4(1.0f));

        static int tri_log = 0;
        if (tri_log < 3) {
            std::fprintf(stderr, "triangle draw: vbuf.id=%d ibuf.id=%d idx_count=%u\n",
                debug_triangle_mesh_.vertex_buffer.id,
                debug_triangle_mesh_.index_buffer.id,
                debug_triangle_mesh_.index_count);
            tri_log++;
        }
        draw_mesh(debug_triangle_mesh_, glm::mat4(1.0f));
    }

    sg_end_pass();
    sg_commit();
}
