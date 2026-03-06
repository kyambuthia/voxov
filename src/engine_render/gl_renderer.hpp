#pragma once

#include "engine_render/render_backend.hpp"

#include <cstdint>
#include <glm/mat4x4.hpp>

struct GLFWwindow;

class GLRenderer : public IRenderBackend {
public:
  void init(void *window_handle) override;
  void shutdown() override;
  void upload_scene(const RenderScene &scene) override;
  void update_dynamic_meshes(const RenderMesh &debug_world,
                             const RenderMesh &debug_screen) override;
  void begin_frame(const RenderFrameContext &ctx,
                   const RenderStats &stats) override;
  void end_frame() override;

private:
  struct UploadedMesh {
    unsigned int vertex_buffer = 0;
    unsigned int index_buffer = 0;
    uint32_t index_count = 0;
  };

  bool init_pipeline();
  void shutdown_pipeline();
  void destroy_uploaded_mesh(UploadedMesh &mesh);
  void upload_mesh(UploadedMesh &mesh, const RenderMesh &source);
  void draw_mesh(const UploadedMesh &mesh, const glm::mat4 &mvp) const;

  GLFWwindow *window = nullptr;
  RenderScene scene;
  bool imgui_ready = false;
  unsigned int program = 0;
  UploadedMesh static_mesh;
  UploadedMesh debug_world_mesh;
  UploadedMesh debug_screen_mesh;
  int uniform_mvp = -1;
  uint64_t last_debug_world_hash = 0;
  uint64_t last_debug_screen_hash = 0;
  bool has_dynamic_mesh_hash = false;
};
