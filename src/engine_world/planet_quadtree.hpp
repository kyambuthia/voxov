#pragma once

#include "engine_render/render_types.hpp"
#include "engine_world/planet_types.hpp"

#include <array>
#include <cstdint>
#include <vector>

enum class QuadtreeNodeState : uint8_t {
  Empty = 0,
  Requested = 1,
  Generating = 2,
  Meshing = 3,
  GpuReady = 4,
  Resident = 5,
};

struct PlanetQuadtreeNode {
  PlanetChunkId id;
  QuadtreeNodeState state = QuadtreeNodeState::Empty;

  glm::dvec3 bounds_min{0.0};
  glm::dvec3 bounds_max{0.0};

  glm::vec3 render_bounds_min{0.0f};
  glm::vec3 render_bounds_max{0.0f};

  std::array<int32_t, 4> children{-1, -1, -1, -1};

  int32_t parent = -1;
  uint64_t last_used_frame = 0;
  int32_t mesh_handle = -1;
  float geometric_error = 0.0f;
  bool has_skirts = false;
};

class PlanetQuadtree {
public:
  PlanetQuadtree() = default;

  void init(const PlanetDefinition &planet, uint32_t max_lod);

  PlanetQuadtreeNode *node(int32_t index);
  const PlanetQuadtreeNode *node(int32_t index) const;

  int32_t root_index(PlanetFace face) const;

  bool subdivide(int32_t node_index);

  int32_t node_count() const { return static_cast<int32_t>(nodes_.size()); }
  const PlanetDefinition &planet() const { return planet_; }
  uint32_t max_lod() const { return max_lod_; }

private:
  PlanetDefinition planet_{};
  uint32_t max_lod_ = 0;
  std::vector<PlanetQuadtreeNode> nodes_;
  std::array<int32_t, 6> face_roots_{-1, -1, -1, -1, -1, -1};
};
