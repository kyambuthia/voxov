#pragma once

#include "engine_render/render_types.hpp"

#include <array>
#include <cstdint>
#include <glm/glm.hpp>

struct VoxelParticle {
  glm::vec3 position{0.0f};
  glm::vec3 velocity{0.0f};
  glm::vec3 color{1.0f};
  float life = 0.0f;
  float max_life = 0.0f;
  float size = 0.14f;
  bool active = false;
};

class VoxelParticleSystem {
public:
  static constexpr size_t kMaxParticles = 256;

  void clear();
  void emit_burst(const glm::vec3 &origin, const glm::vec3 &up,
                  const glm::vec3 &color, int count, float speed = 3.5f);
  void step(float dt, const glm::vec3 &gravity_dir = glm::vec3(0.0f, -1.0f, 0.0f));
  void append_to_mesh(RenderMesh &mesh, const glm::vec3 &camera_pos) const;
  size_t active_count() const;

private:
  std::array<VoxelParticle, kMaxParticles> pool_{};
  size_t next_slot_ = 0;
  uint32_t rng_state_ = 0x12345678u;
};
