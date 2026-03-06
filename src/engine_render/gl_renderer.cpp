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
};

uint64_t mesh_content_hash(const RenderMesh &mesh) {
  constexpr uint64_t k_fnv_offset = 1469598103934665603ull;
  constexpr uint64_t k_fnv_prime = 1099511628211ull;
  auto hash_bytes = [](const uint8_t *data, size_t len, uint64_t seed) {
    uint64_t h = seed;
    for (size_t i = 0; i < len; ++i) {
      h ^= static_cast<uint64_t>(data[i]);
      h *= k_fnv_prime;
    }
    return h;
  };

  uint64_t h = k_fnv_offset;
  const size_t vertex_count = mesh.vertices.size();
  const size_t index_count = mesh.indices.size();
  h = hash_bytes(reinterpret_cast<const uint8_t *>(&vertex_count),
                 sizeof(size_t), h);
  h = hash_bytes(reinterpret_cast<const uint8_t *>(&index_count),
                 sizeof(size_t), h);
  if (!mesh.vertices.empty()) {
    h = hash_bytes(reinterpret_cast<const uint8_t *>(mesh.vertices.data()),
                   mesh.vertices.size() * sizeof(RenderVertex), h);
  }
  if (!mesh.indices.empty()) {
    h = hash_bytes(reinterpret_cast<const uint8_t *>(mesh.indices.data()),
                   mesh.indices.size() * sizeof(uint32_t), h);
  }
  return h;
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
      load_gl_proc("glDisableVertexAttribArray", g_disable_vertex_attrib_array);
  if (!loaded) {
    return false;
  }
#endif

  static const char *k_vs = R"(
        #version 130
        in vec3 a_pos;
        in vec3 a_color;
        uniform mat4 u_mvp;
        out vec3 v_color;
        void main() {
            v_color = a_color;
            gl_Position = u_mvp * vec4(a_pos, 1.0);
        }
    )";

  static const char *k_fs = R"(
        #version 130
        in vec3 v_color;
        out vec4 frag_color;
        void main() {
            frag_color = vec4(v_color, 1.0);
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
  glLinkProgram(program);
  glDeleteShader(vs);
  glDeleteShader(fs);
#else
  program = g_create_program();
  g_attach_shader(program, vs);
  g_attach_shader(program, fs);
  g_bind_attrib_location(program, 0, "a_pos");
  g_bind_attrib_location(program, 1, "a_color");
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
  glDisable(GL_CULL_FACE);

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
  destroy_uploaded_mesh(static_mesh);
  destroy_uploaded_mesh(debug_world_mesh);
  destroy_uploaded_mesh(debug_screen_mesh);
  shutdown_pipeline();
  scene = RenderScene{};
  has_dynamic_mesh_hash = false;
  last_debug_world_hash = 0;
  last_debug_screen_hash = 0;
}

void GLRenderer::upload_scene(const RenderScene &new_scene) {
  scene = new_scene;
  RenderMesh static_scene{};
  auto append_mesh = [&](const RenderMesh &mesh) {
    const uint32_t base = static_cast<uint32_t>(static_scene.vertices.size());
    static_scene.vertices.insert(static_scene.vertices.end(),
                                 mesh.vertices.begin(), mesh.vertices.end());
    for (uint32_t idx : mesh.indices) {
      static_scene.indices.push_back(base + idx);
    }
  };
  for (const RenderMesh &mesh : scene.opaque_meshes) {
    append_mesh(mesh);
  }
  append_mesh(scene.debug_grid);

  upload_mesh(static_mesh, static_scene);
  upload_mesh(debug_world_mesh, scene.debug_world);
  upload_mesh(debug_screen_mesh, scene.debug_screen);
  last_debug_world_hash = mesh_content_hash(scene.debug_world);
  last_debug_screen_hash = mesh_content_hash(scene.debug_screen);
  has_dynamic_mesh_hash = true;
}

void GLRenderer::update_dynamic_meshes(const RenderMesh &debug_world,
                                       const RenderMesh &debug_screen) {
  const uint64_t world_hash = mesh_content_hash(debug_world);
  const uint64_t screen_hash = mesh_content_hash(debug_screen);
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
  mesh.index_count = 0;
}

void GLRenderer::upload_mesh(UploadedMesh &mesh, const RenderMesh &source) {
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

  std::vector<GlRenderVertex> vertices;
  vertices.reserve(source.vertices.size());
  for (const RenderVertex &v : source.vertices) {
    vertices.push_back({v.position.x, v.position.y, v.position.z, v.color.r,
                        v.color.g, v.color.b});
  }

#if defined(__APPLE__)
  glBindBuffer(GL_ARRAY_BUFFER, mesh.vertex_buffer);
  glBufferData(
      GL_ARRAY_BUFFER,
      static_cast<GLsizeiptr>(vertices.size() * sizeof(GlRenderVertex)),
      vertices.data(), GL_STATIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.index_buffer);
  glBufferData(
      GL_ELEMENT_ARRAY_BUFFER,
      static_cast<GLsizeiptr>(source.indices.size() * sizeof(uint32_t)),
      source.indices.data(), GL_STATIC_DRAW);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
#else
  g_bind_buffer(GL_ARRAY_BUFFER, mesh.vertex_buffer);
  g_buffer_data(
      GL_ARRAY_BUFFER,
      static_cast<GLsizeiptr>(vertices.size() * sizeof(GlRenderVertex)),
      vertices.data(), GL_STATIC_DRAW);
  g_bind_buffer(GL_ELEMENT_ARRAY_BUFFER, mesh.index_buffer);
  g_buffer_data(
      GL_ELEMENT_ARRAY_BUFFER,
      static_cast<GLsizeiptr>(source.indices.size() * sizeof(uint32_t)),
      source.indices.data(), GL_STATIC_DRAW);
  g_bind_buffer(GL_ARRAY_BUFFER, 0);
  g_bind_buffer(GL_ELEMENT_ARRAY_BUFFER, 0);
#endif

  mesh.index_count = static_cast<uint32_t>(source.indices.size());
}

void GLRenderer::draw_mesh(const UploadedMesh &mesh,
                           const glm::mat4 &mvp) const {
  if (program == 0 || mesh.vertex_buffer == 0 || mesh.index_buffer == 0 ||
      mesh.index_count == 0) {
    return;
  }

#if defined(__APPLE__)
  glUseProgram(program);
  glUniformMatrix4fv(uniform_mvp, 1, GL_FALSE, glm::value_ptr(mvp));
  glBindBuffer(GL_ARRAY_BUFFER, mesh.vertex_buffer);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.index_buffer);
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(GlRenderVertex),
                        reinterpret_cast<const void *>(0));
  glVertexAttribPointer(
      1, 3, GL_FLOAT, GL_FALSE, sizeof(GlRenderVertex),
      reinterpret_cast<const void *>(offsetof(GlRenderVertex, cr)));
#else
  g_use_program(program);
  g_uniform_matrix4fv(uniform_mvp, 1, GL_FALSE, glm::value_ptr(mvp));
  g_bind_buffer(GL_ARRAY_BUFFER, mesh.vertex_buffer);
  g_bind_buffer(GL_ELEMENT_ARRAY_BUFFER, mesh.index_buffer);
  g_enable_vertex_attrib_array(0);
  g_enable_vertex_attrib_array(1);
  g_vertex_attrib_pointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(GlRenderVertex),
                          reinterpret_cast<const void *>(0));
  g_vertex_attrib_pointer(
      1, 3, GL_FLOAT, GL_FALSE, sizeof(GlRenderVertex),
      reinterpret_cast<const void *>(offsetof(GlRenderVertex, cr)));
#endif

  glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.index_count),
                 GL_UNSIGNED_INT, reinterpret_cast<const void *>(0));

#if defined(__APPLE__)
  glDisableVertexAttribArray(0);
  glDisableVertexAttribArray(1);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  glUseProgram(0);
#else
  g_disable_vertex_attrib_array(0);
  g_disable_vertex_attrib_array(1);
  g_bind_buffer(GL_ARRAY_BUFFER, 0);
  g_bind_buffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  g_use_program(0);
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

    draw_mesh(static_mesh, view_proj);

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
