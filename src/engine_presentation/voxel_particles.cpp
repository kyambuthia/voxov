#include "engine_presentation/voxel_particles.hpp"

#include <algorithm>
#include <cmath>

namespace {
float next_unit_float(uint32_t &state) {
  state ^= state << 13u;
  state ^= state >> 17u;
  state ^= state << 5u;
  return static_cast<float>(state & 0x00FFFFFFu) /
         static_cast<float>(0x01000000u);
}
} // namespace

void VoxelParticleSystem::clear() {
  for (VoxelParticle &p : pool_) {
    p.active = false;
  }
}

void VoxelParticleSystem::emit_burst(const glm::vec3 &origin,
                                     const glm::vec3 &up,
                                     const glm::vec3 &color, int count,
                                     float speed) {
  const glm::vec3 n_up =
      glm::dot(up, up) > 1.0e-6f ? glm::normalize(up) : glm::vec3(0.0f, 1.0f, 0.0f);
  for (int i = 0; i < count; ++i) {
    VoxelParticle &p = pool_[next_slot_];
    next_slot_ = (next_slot_ + 1u) % kMaxParticles;
    const float rx = next_unit_float(rng_state_) * 2.0f - 1.0f;
    const float ry = 0.4f + next_unit_float(rng_state_) * 0.9f;
    const float rz = next_unit_float(rng_state_) * 2.0f - 1.0f;
    glm::vec3 dir(rx, ry, rz);
    if (glm::dot(dir, n_up) < 0.0f) {
      dir = -dir;
    }
    p.position = origin + glm::vec3(rx * 0.25f, 0.0f, rz * 0.25f);
    p.velocity = glm::normalize(dir) * (speed * (0.5f + 0.7f * next_unit_float(rng_state_)));
    p.color = color;
    p.max_life = 0.45f + 0.35f * next_unit_float(rng_state_);
    p.life = p.max_life;
    p.size = 0.08f + 0.08f * next_unit_float(rng_state_);
    p.active = true;
  }
}

void VoxelParticleSystem::step(float dt, const glm::vec3 &gravity_dir) {
  if (dt <= 0.0f) {
    return;
  }
  const glm::vec3 grav = gravity_dir * 12.0f;
  for (VoxelParticle &p : pool_) {
    if (!p.active) {
      continue;
    }
    p.life -= dt;
    if (p.life <= 0.0f) {
      p.active = false;
      continue;
    }
    p.velocity += grav * dt;
    p.position += p.velocity * dt;
  }
}

void VoxelParticleSystem::append_to_mesh(RenderMesh &mesh,
                                         const glm::vec3 &camera_pos) const {
  (void)camera_pos;
  for (const VoxelParticle &p : pool_) {
    if (!p.active) {
      continue;
    }
    const float s = p.size * std::clamp(p.life / std::max(0.01f, p.max_life), 0.2f, 1.0f);
    const glm::vec3 c = p.position;
    const glm::vec3 n(0.0f, 1.0f, 0.0f);
    const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back({c + glm::vec3(-s, -s, 0.0f), p.color, n, glm::vec3(0.0f)});
    mesh.vertices.push_back({c + glm::vec3( s, -s, 0.0f), p.color, n, glm::vec3(1.0f, 0.0f, 0.0f)});
    mesh.vertices.push_back({c + glm::vec3( s,  s, 0.0f), p.color, n, glm::vec3(1.0f, 1.0f, 0.0f)});
    mesh.vertices.push_back({c + glm::vec3(-s,  s, 0.0f), p.color, n, glm::vec3(0.0f, 1.0f, 0.0f)});
    mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
  }
}

size_t VoxelParticleSystem::active_count() const {
  size_t count = 0;
  for (const VoxelParticle &p : pool_) {
    if (p.active) {
      ++count;
    }
  }
  return count;
}
