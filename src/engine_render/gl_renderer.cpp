#include "engine_render/gl_renderer.hpp"

#include <GLFW/glfw3.h>
#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#include <GL/glext.h>
#endif

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

namespace {
struct GlRenderVertex {
  float px;
  float py;
  float pz;
  float cr;
  float cg;
  float cb;
  float nx;
  float ny;
  float nz;
};

// O(1) change-detection token: encodes vertex count and index count.
// Catches every structural change (mesh rebuilt) at zero per-byte cost.
static uint64_t mesh_size_token(const RenderMesh &mesh) {
  return (static_cast<uint64_t>(mesh.vertices.size()) << 32) |
         static_cast<uint64_t>(mesh.indices.size());
}

struct Frustum {
  glm::vec4 planes[6];
};

Frustum extract_frustum(const glm::mat4 &vp) {
  // GLM is column-major: vp[col][row], so row i = (vp[0][i], vp[1][i], vp[2][i], vp[3][i])
  auto row = [&](int i) {
    return glm::vec4(vp[0][i], vp[1][i], vp[2][i], vp[3][i]);
  };
  const glm::vec4 r0 = row(0);
  const glm::vec4 r1 = row(1);
  const glm::vec4 r2 = row(2);
  const glm::vec4 r3 = row(3);
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
        pl.x >= 0.0f ? bmax.x : bmin.x,
        pl.y >= 0.0f ? bmax.y : bmin.y,
        pl.z >= 0.0f ? bmax.z : bmin.z);
    if (pl.x * pv.x + pl.y * pv.y + pl.z * pv.z + pl.w < 0.0f) {
      return false;
    }
  }
  return true;
}

GLuint compile_shader(GLenum type, const char *source) {
#if defined(__APPLE__)
  const GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);
#else
  extern PFNGLCREATESHADERPROC g_create_shader;
  extern PFNGLSHADERSOURCEPROC g_shader_source;
  extern PFNGLCOMPILESHADERPROC g_compile_shader;
  const GLuint shader = g_create_shader(type);
  g_shader_source(shader, 1, &source, nullptr);
  g_compile_shader(shader);
#endif

  GLint ok = GL_FALSE;
#if defined(__APPLE__)
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
#else
  extern PFNGLGETSHADERIVPROC g_get_shader_iv;
  g_get_shader_iv(shader, GL_COMPILE_STATUS, &ok);
#endif
  if (ok == GL_FALSE) {
#if defined(__APPLE__)
    glDeleteShader(shader);
#else
    extern PFNGLDELETESHADERPROC g_delete_shader;
    g_delete_shader(shader);
#endif
    return 0;
  }
  return shader;
}

#if !defined(__APPLE__)
PFNGLCREATESHADERPROC g_create_shader = nullptr;
PFNGLSHADERSOURCEPROC g_shader_source = nullptr;
PFNGLCOMPILESHADERPROC g_compile_shader = nullptr;
PFNGLGETSHADERIVPROC g_get_shader_iv = nullptr;
PFNGLDELETESHADERPROC g_delete_shader = nullptr;
PFNGLCREATEPROGRAMPROC g_create_program = nullptr;
PFNGLATTACHSHADERPROC g_attach_shader = nullptr;
PFNGLBINDATTRIBLOCATIONPROC g_bind_attrib_location = nullptr;
PFNGLLINKPROGRAMPROC g_link_program = nullptr;
PFNGLGETPROGRAMIVPROC g_get_program_iv = nullptr;
PFNGLDELETEPROGRAMPROC g_delete_program = nullptr;
PFNGLGETUNIFORMLOCATIONPROC g_get_uniform_location = nullptr;
PFNGLGENBUFFERSPROC g_gen_buffers = nullptr;
PFNGLDELETEBUFFERSPROC g_delete_buffers = nullptr;
PFNGLUSEPROGRAMPROC g_use_program = nullptr;
PFNGLUNIFORMMATRIX4FVPROC g_uniform_matrix4fv = nullptr;
PFNGLBINDBUFFERPROC g_bind_buffer = nullptr;
PFNGLBUFFERDATAPROC g_buffer_data = nullptr;
PFNGLENABLEVERTEXATTRIBARRAYPROC g_enable_vertex_attrib_array = nullptr;
PFNGLVERTEXATTRIBPOINTERPROC g_vertex_attrib_pointer = nullptr;
PFNGLDISABLEVERTEXATTRIBARRAYPROC g_disable_vertex_attrib_array = nullptr;
PFNGLGENVERTEXARRAYSPROC g_gen_vertex_arrays = nullptr;
PFNGLBINDVERTEXARRAYPROC g_bind_vertex_array = nullptr;
PFNGLDELETEVERTEXARRAYSPROC g_delete_vertex_arrays = nullptr;

template <typename T> bool load_gl_proc(const char *name, T &fn_ptr) {
  fn_ptr = reinterpret_cast<T>(glfwGetProcAddress(name));
  return fn_ptr != nullptr;
}
#endif
} // namespace

bool GLRenderer::init_pipeline() {
#if !defined(__APPLE__)
  const bool loaded =
      load_gl_proc("glCreateShader", g_create_shader) &&
      load_gl_proc("glShaderSource", g_shader_source) &&
      load_gl_proc("glCompileShader", g_compile_shader) &&
      load_gl_proc("glGetShaderiv", g_get_shader_iv) &&
      load_gl_proc("glDeleteShader", g_delete_shader) &&
      load_gl_proc("glCreateProgram", g_create_program) &&
      load_gl_proc("glAttachShader", g_attach_shader) &&
      load_gl_proc("glBindAttribLocation", g_bind_attrib_location) &&
      load_gl_proc("glLinkProgram", g_link_program) &&
      load_gl_proc("glGetProgramiv", g_get_program_iv) &&
      load_gl_proc("glDeleteProgram", g_delete_program) &&
      load_gl_proc("glGetUniformLocation", g_get_uniform_location) &&
      load_gl_proc("glGenBuffers", g_gen_buffers) &&
      load_gl_proc("glDeleteBuffers", g_delete_buffers) &&
      load_gl_proc("glUseProgram", g_use_program) &&
      load_gl_proc("glUniformMatrix4fv", g_uniform_matrix4fv) &&
      load_gl_proc("glBindBuffer", g_bind_buffer) &&
      load_gl_proc("glBufferData", g_buffer_data) &&
      load_gl_proc("glEnableVertexAttribArray", g_enable_vertex_attrib_array) &&
      load_gl_proc("glVertexAttribPointer", g_vertex_attrib_pointer) &&
      load_gl_proc("glDisableVertexAttribArray", g_disable_vertex_attrib_array) &&
      load_gl_proc("glGenVertexArrays", g_gen_vertex_arrays) &&
      load_gl_proc("glBindVertexArray", g_bind_vertex_array) &&
      load_gl_proc("glDeleteVertexArrays", g_delete_vertex_arrays);
  if (!loaded) {
    return false;
  }
#endif

  static const char *k_vs = R"(
        #version 130
        in vec3 a_pos;
        in vec3 a_color;
        in vec3 a_normal;
        uniform mat4 u_mvp;
        out vec3 v_color;
        out vec3 v_normal;
        void main() {
            v_color = a_color;
            v_normal = a_normal;
            gl_Position = u_mvp * vec4(a_pos, 1.0);
        }
    )";

  static const char *k_fs = R"(
        #version 130
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

  const GLuint vs = compile_shader(GL_VERTEX_SHADER, k_vs);
  const GLuint fs = compile_shader(GL_FRAGMENT_SHADER, k_fs);
  if (vs == 0 || fs == 0) {
    if (vs != 0) {
#if defined(__APPLE__)
      glDeleteShader(vs);
#else
      g_delete_shader(vs);
#endif
    }
    if (fs != 0) {
#if defined(__APPLE__)
      glDeleteShader(fs);
#else
      g_delete_shader(fs);
#endif
    }
    return false;
  }

#if defined(__APPLE__)
  program = glCreateProgram();
  glAttachShader(program, vs);
  glAttachShader(program, fs);
  glBindAttribLocation(program, 0, "a_pos");
  glBindAttribLocation(program, 1, "a_color");
  glBindAttribLocation(program, 2, "a_normal");
  glLinkProgram(program);
  glDeleteShader(vs);
  glDeleteShader(fs);
#else
  program = g_create_program();
  g_attach_shader(program, vs);
  g_attach_shader(program, fs);
  g_bind_attrib_location(program, 0, "a_pos");
  g_bind_attrib_location(program, 1, "a_color");
  g_bind_attrib_location(program, 2, "a_normal");
  g_link_program(program);
  g_delete_shader(vs);
  g_delete_shader(fs);
#endif

  GLint linked = GL_FALSE;
#if defined(__APPLE__)
  glGetProgramiv(program, GL_LINK_STATUS, &linked);
#else
  g_get_program_iv(program, GL_LINK_STATUS, &linked);
#endif
  if (linked == GL_FALSE) {
#if defined(__APPLE__)
    glDeleteProgram(program);
#else
    g_delete_program(program);
#endif
    program = 0;
    return false;
  }

#if defined(__APPLE__)
  uniform_mvp = glGetUniformLocation(program, "u_mvp");
#else
  uniform_mvp = g_get_uniform_location(program, "u_mvp");
#endif
  if (uniform_mvp < 0) {
#if defined(__APPLE__)
    glDeleteProgram(program);
#else
    g_delete_program(program);
#endif
    program = 0;
    return false;
  }

  return true;
}

void GLRenderer::shutdown_pipeline() {
  if (program != 0) {
#if defined(__APPLE__)
    glDeleteProgram(program);
#else
    g_delete_program(program);
#endif
    program = 0;
  }
  uniform_mvp = -1;
}

void GLRenderer::init(void *window_handle) {
  window = static_cast<GLFWwindow *>(window_handle);
  glfwMakeContextCurrent(window);
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_CULL_FACE);
  glCullFace(GL_BACK);
  glFrontFace(GL_CCW);

  (void)init_pipeline();

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  ImGui::StyleColorsDark();

  if (ImGui_ImplGlfw_InitForOpenGL(window, true) &&
      ImGui_ImplOpenGL3_Init("#version 130")) {
    imgui_ready = true;
  }
}

void GLRenderer::shutdown() {
  if (imgui_ready) {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    imgui_ready = false;
  }
  if (ImGui::GetCurrentContext() != nullptr) {
    ImGui::DestroyContext();
  }
  destroy_uploaded_mesh(transient_mesh);
  destroy_uploaded_mesh(debug_grid_mesh);
  destroy_uploaded_mesh(debug_world_mesh);
  destroy_uploaded_mesh(debug_screen_mesh);
  for (auto &[id, mesh] : cached_meshes_) {
    destroy_uploaded_mesh(mesh);
  }
  cached_meshes_.clear();
  shutdown_pipeline();
  scene = RenderScene{};
  has_dynamic_mesh_hash = false;
  last_debug_world_hash = 0;
  last_debug_screen_hash = 0;
}

void GLRenderer::upload_scene(const RenderScene &new_scene) {
  scene = new_scene;

  // Evict cached GPU meshes that are no longer in the scene
  std::unordered_map<uint64_t, bool> live_ids;
  for (const RenderMesh &mesh : scene.opaque_meshes) {
    if (mesh.mesh_id != 0) {
      live_ids[mesh.mesh_id] = true;
    }
  }
  std::vector<uint64_t> stale_ids;
  for (const auto &[id, _] : cached_meshes_) {
    if (live_ids.find(id) == live_ids.end()) {
      stale_ids.push_back(id);
    }
  }
  for (uint64_t id : stale_ids) {
    destroy_uploaded_mesh(cached_meshes_[id]);
    cached_meshes_.erase(id);
  }

  // Build transient mesh from all meshes with mesh_id == 0 (sky, planet)
  RenderMesh transient_scene{};
  auto append_mesh = [&](const RenderMesh &mesh) {
    const uint32_t base = static_cast<uint32_t>(transient_scene.vertices.size());
    transient_scene.vertices.insert(transient_scene.vertices.end(),
                                    mesh.vertices.begin(), mesh.vertices.end());
    for (uint32_t idx : mesh.indices) {
      transient_scene.indices.push_back(base + idx);
    }
  };
  for (const RenderMesh &mesh : scene.opaque_meshes) {
    if (mesh.mesh_id == 0) {
      append_mesh(mesh);
    } else if (cached_meshes_.find(mesh.mesh_id) == cached_meshes_.end()) {
      UploadedMesh uploaded{};
      upload_mesh(uploaded, mesh);
      cached_meshes_[mesh.mesh_id] = uploaded;
    }
  }

  upload_mesh(transient_mesh, transient_scene, GL_DYNAMIC_DRAW);
  upload_mesh(debug_grid_mesh, scene.debug_grid);
  upload_mesh(debug_world_mesh, scene.debug_world);
  upload_mesh(debug_screen_mesh, scene.debug_screen);
  last_debug_world_hash = mesh_size_token(scene.debug_world);
  last_debug_screen_hash = mesh_size_token(scene.debug_screen);
  has_dynamic_mesh_hash = true;
}

void GLRenderer::update_dynamic_meshes(const RenderMesh &debug_world,
                                       const RenderMesh &debug_screen) {
  const uint64_t world_hash = mesh_size_token(debug_world);
  const uint64_t screen_hash = mesh_size_token(debug_screen);
  if (has_dynamic_mesh_hash && world_hash == last_debug_world_hash &&
      screen_hash == last_debug_screen_hash) {
    return;
  }

  scene.debug_world = debug_world;
  scene.debug_screen = debug_screen;
  upload_mesh(debug_world_mesh, scene.debug_world);
  upload_mesh(debug_screen_mesh, scene.debug_screen);
  last_debug_world_hash = world_hash;
  last_debug_screen_hash = screen_hash;
  has_dynamic_mesh_hash = true;
}

void GLRenderer::destroy_uploaded_mesh(UploadedMesh &mesh) {
  if (mesh.index_buffer != 0) {
#if defined(__APPLE__)
    glDeleteBuffers(1, &mesh.index_buffer);
#else
    g_delete_buffers(1, &mesh.index_buffer);
#endif
    mesh.index_buffer = 0;
  }
  if (mesh.vertex_buffer != 0) {
#if defined(__APPLE__)
    glDeleteBuffers(1, &mesh.vertex_buffer);
#else
    g_delete_buffers(1, &mesh.vertex_buffer);
#endif
    mesh.vertex_buffer = 0;
  }
  if (mesh.vertex_array != 0) {
#if defined(__APPLE__)
    glDeleteVertexArrays(1, &mesh.vertex_array);
#else
    g_delete_vertex_arrays(1, &mesh.vertex_array);
#endif
    mesh.vertex_array = 0;
  }
  mesh.index_count = 0;
}

void GLRenderer::upload_mesh(UploadedMesh &mesh, const RenderMesh &source,
                             unsigned int usage) {
  if (source.vertices.empty() || source.indices.empty()) {
    destroy_uploaded_mesh(mesh);
    return;
  }

  if (mesh.vertex_buffer == 0) {
#if defined(__APPLE__)
    glGenBuffers(1, &mesh.vertex_buffer);
#else
    g_gen_buffers(1, &mesh.vertex_buffer);
#endif
  }
  if (mesh.index_buffer == 0) {
#if defined(__APPLE__)
    glGenBuffers(1, &mesh.index_buffer);
#else
    g_gen_buffers(1, &mesh.index_buffer);
#endif
  }
  if (mesh.vertex_array == 0) {
#if defined(__APPLE__)
    glGenVertexArrays(1, &mesh.vertex_array);
#else
    g_gen_vertex_arrays(1, &mesh.vertex_array);
#endif
  }

  std::vector<GlRenderVertex> vertices;
  vertices.reserve(source.vertices.size());
  glm::vec3 bmin(std::numeric_limits<float>::max());
  glm::vec3 bmax(-std::numeric_limits<float>::max());
  for (const RenderVertex &v : source.vertices) {
    vertices.push_back({v.position.x, v.position.y, v.position.z, v.color.r,
                        v.color.g, v.color.b, v.normal.x, v.normal.y,
                        v.normal.z});
    bmin = glm::min(bmin, v.position);
    bmax = glm::max(bmax, v.position);
  }
  mesh.bounds_min = bmin;
  mesh.bounds_max = bmax;

  // Record buffer bindings and attrib layout inside the VAO.
  // Note: GL_ELEMENT_ARRAY_BUFFER is part of VAO state; unbind VAO before
  // touching the IBO binding externally.
#if defined(__APPLE__)
  glBindVertexArray(mesh.vertex_array);
  glBindBuffer(GL_ARRAY_BUFFER, mesh.vertex_buffer);
  glBufferData(
      GL_ARRAY_BUFFER,
      static_cast<GLsizeiptr>(vertices.size() * sizeof(GlRenderVertex)),
      vertices.data(), static_cast<GLenum>(usage));
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.index_buffer);
  glBufferData(
      GL_ELEMENT_ARRAY_BUFFER,
      static_cast<GLsizeiptr>(source.indices.size() * sizeof(uint32_t)),
      source.indices.data(), static_cast<GLenum>(usage));
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(GlRenderVertex),
                        reinterpret_cast<const void *>(0));
  glVertexAttribPointer(
      1, 3, GL_FLOAT, GL_FALSE, sizeof(GlRenderVertex),
      reinterpret_cast<const void *>(offsetof(GlRenderVertex, cr)));
  glVertexAttribPointer(
      2, 3, GL_FLOAT, GL_FALSE, sizeof(GlRenderVertex),
      reinterpret_cast<const void *>(offsetof(GlRenderVertex, nx)));
  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
#else
  g_bind_vertex_array(mesh.vertex_array);
  g_bind_buffer(GL_ARRAY_BUFFER, mesh.vertex_buffer);
  g_buffer_data(
      GL_ARRAY_BUFFER,
      static_cast<GLsizeiptr>(vertices.size() * sizeof(GlRenderVertex)),
      vertices.data(), static_cast<GLenum>(usage));
  g_bind_buffer(GL_ELEMENT_ARRAY_BUFFER, mesh.index_buffer);
  g_buffer_data(
      GL_ELEMENT_ARRAY_BUFFER,
      static_cast<GLsizeiptr>(source.indices.size() * sizeof(uint32_t)),
      source.indices.data(), static_cast<GLenum>(usage));
  g_enable_vertex_attrib_array(0);
  g_enable_vertex_attrib_array(1);
  g_enable_vertex_attrib_array(2);
  g_vertex_attrib_pointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(GlRenderVertex),
                          reinterpret_cast<const void *>(0));
  g_vertex_attrib_pointer(
      1, 3, GL_FLOAT, GL_FALSE, sizeof(GlRenderVertex),
      reinterpret_cast<const void *>(offsetof(GlRenderVertex, cr)));
  g_vertex_attrib_pointer(
      2, 3, GL_FLOAT, GL_FALSE, sizeof(GlRenderVertex),
      reinterpret_cast<const void *>(offsetof(GlRenderVertex, nx)));
  g_bind_vertex_array(0);
  g_bind_buffer(GL_ARRAY_BUFFER, 0);
#endif

  mesh.index_count = static_cast<uint32_t>(source.indices.size());
}

void GLRenderer::draw_mesh(const UploadedMesh &mesh,
                           const glm::mat4 &mvp) const {
  if (program == 0 || mesh.vertex_array == 0 || mesh.index_count == 0) {
    return;
  }

#if defined(__APPLE__)
  glUniformMatrix4fv(uniform_mvp, 1, GL_FALSE, glm::value_ptr(mvp));
  glBindVertexArray(mesh.vertex_array);
#else
  g_uniform_matrix4fv(uniform_mvp, 1, GL_FALSE, glm::value_ptr(mvp));
  g_bind_vertex_array(mesh.vertex_array);
#endif

  glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.index_count),
                 GL_UNSIGNED_INT, reinterpret_cast<const void *>(0));

#if defined(__APPLE__)
  glBindVertexArray(0);
#else
  g_bind_vertex_array(0);
#endif
}

void GLRenderer::begin_frame(const RenderFrameContext &ctx,
                             const RenderStats &stats) {
  int width = 0;
  int height = 0;
  glfwGetFramebufferSize(window, &width, &height);
  if (height <= 0) {
    height = 1;
  }

  glViewport(0, 0, width, height);
  glClearColor(0.08f, 0.1f, 0.14f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  const uint32_t view_count = std::max(1u, std::min(ctx.view_count, 2u));

  // Bind the shader program once for all draw calls this frame.
#if defined(__APPLE__)
  glUseProgram(program);
#else
  if (program != 0) { g_use_program(program); }
#endif

  for (uint32_t i = 0; i < view_count; ++i) {
    const RenderView &view = ctx.views[i];
    const int vx =
        static_cast<int>(view.viewport.x * static_cast<float>(width));
    const int vy =
        static_cast<int>(view.viewport.y * static_cast<float>(height));
    const int vw = std::max(
        1, static_cast<int>(view.viewport.z * static_cast<float>(width)));
    const int vh = std::max(
        1, static_cast<int>(view.viewport.w * static_cast<float>(height)));

    glViewport(vx, vy, vw, vh);
    const glm::mat4 p =
        glm::perspective(view.camera.fov_y_radians,
                         static_cast<float>(vw) / static_cast<float>(vh),
                         view.camera.z_near, view.camera.z_far);
    const glm::mat4 view_proj = p * view.camera.view();

    draw_mesh(transient_mesh, view_proj);

    // Draw per-chunk cached meshes with frustum culling
    const Frustum frustum = extract_frustum(view_proj);
    for (const auto &[id, mesh] : cached_meshes_) {
      if (aabb_in_frustum(frustum, mesh.bounds_min, mesh.bounds_max)) {
        draw_mesh(mesh, view_proj);
      }
    }

    // Debug grid uses flat horizontal quads — disable culling while drawing
    glDisable(GL_CULL_FACE);
    draw_mesh(debug_grid_mesh, view_proj);
    glEnable(GL_CULL_FACE);

    if (ctx.debug_xray) {
      glDisable(GL_DEPTH_TEST);
      draw_mesh(debug_world_mesh, view_proj);
      glEnable(GL_DEPTH_TEST);
    } else {
      draw_mesh(debug_world_mesh, view_proj);
    }

    const glm::mat4 screen_mvp(1.0f);
    glDisable(GL_DEPTH_TEST);
    draw_mesh(debug_screen_mesh, screen_mvp);
    glEnable(GL_DEPTH_TEST);
  }

  // Release program after all geometry draws.
#if defined(__APPLE__)
  glUseProgram(0);
#else
  if (program != 0) { g_use_program(0); }
#endif

  if (imgui_ready) {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    (void)stats;

    if (false && (stats.menu_open || !stats.menu_text.empty())) {
      ImGui::SetNextWindowPos(ImVec2(20.0f, 20.0f), ImGuiCond_Always);
      ImGui::SetNextWindowSize(ImVec2(520.0f, 420.0f), ImGuiCond_Always);
      ImGui::SetNextWindowBgAlpha(0.92f);
      const ImGuiWindowFlags menu_flags = ImGuiWindowFlags_NoCollapse |
                                          ImGuiWindowFlags_NoResize |
                                          ImGuiWindowFlags_NoSavedSettings;
      if (ImGui::Begin("VOXOV Menu", nullptr, menu_flags)) {
        if (stats.menu_open) {
          if (!stats.menu_title.empty()) {
            ImGui::TextUnformatted(stats.menu_title.c_str());
            ImGui::Separator();
          }
          for (size_t i = 0; i < stats.menu_items.size(); ++i) {
            const bool selected = static_cast<int>(i) == stats.menu_selected;
            if (selected) {
              ImGui::PushStyleColor(ImGuiCol_Button,
                                    ImVec4(0.20f, 0.34f, 0.52f, 1.0f));
              ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                    ImVec4(0.24f, 0.40f, 0.60f, 1.0f));
            }
            ImGui::Button(stats.menu_items[i].c_str(), ImVec2(-1.0f, 0.0f));
            if (selected) {
              ImGui::PopStyleColor(2);
            }
          }
          if (!stats.menu_guide.empty()) {
            ImGui::Separator();
            for (const std::string &line : stats.menu_guide) {
              ImGui::TextUnformatted(line.c_str());
            }
          }
          if (!stats.menu_status.empty()) {
            ImGui::Separator();
            ImGui::Text("Status: %s", stats.menu_status.c_str());
          }
        } else {
          ImGui::PushTextWrapPos();
          ImGui::TextUnformatted(stats.menu_text.c_str());
          ImGui::PopTextWrapPos();
        }
      }
      ImGui::End();
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  }
}

void GLRenderer::end_frame() { glfwSwapBuffers(window); }
