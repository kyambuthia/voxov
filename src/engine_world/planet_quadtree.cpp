#include "engine_world/planet_quadtree.hpp"

#include "engine_world/planet_math.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include <glm/common.hpp>

namespace {
constexpr PlanetFace k_faces[] = {
    PlanetFace::PosX, PlanetFace::NegX, PlanetFace::PosY,
    PlanetFace::NegY, PlanetFace::PosZ, PlanetFace::NegZ,
};

int32_t face_index(PlanetFace face) { return static_cast<int32_t>(face); }

double lod_cells(int32_t lod) {
  return static_cast<double>(int32_t{1} << std::max(0, lod));
}

void node_uv_range(const PlanetChunkId &id, double &u0, double &v0, double &u1,
                   double &v1) {
  const double cells = lod_cells(id.lod);
  const double span = 2.0 / cells;
  u0 = -1.0 + span * static_cast<double>(id.x);
  v0 = -1.0 + span * static_cast<double>(id.y);
  u1 = u0 + span;
  v1 = v0 + span;
}

float geometric_error_for_lod(const PlanetDefinition &planet, int32_t lod) {
  const double divisor = lod_cells(lod);
  return static_cast<float>((planet.radius * planet.voxel_size) / divisor);
}

void expand_bounds(glm::dvec3 &bmin, glm::dvec3 &bmax,
                   const glm::dvec3 &point) {
  bmin = glm::min(bmin, point);
  bmax = glm::max(bmax, point);
}

void compute_bounds(const PlanetDefinition &planet, const PlanetChunkId &id,
                    glm::dvec3 &bounds_min, glm::dvec3 &bounds_max) {
  double u0 = 0.0;
  double v0 = 0.0;
  double u1 = 0.0;
  double v1 = 0.0;
  node_uv_range(id, u0, v0, u1, v1);

  bounds_min = glm::dvec3(std::numeric_limits<double>::max());
  bounds_max = glm::dvec3(std::numeric_limits<double>::lowest());

  const double um = (u0 + u1) * 0.5;
  const double vm = (v0 + v1) * 0.5;
  const double us[] = {u0, um, u1};
  const double vs[] = {v0, vm, v1};
  const double terrain_margin = std::max(planet.voxel_size, planet.voxel_size * 16.0);

  for (const double u : us) {
    for (const double v : vs) {
      const glm::dvec3 direction = face_uv_to_direction(id.face, u, v);
      expand_bounds(bounds_min, bounds_max, direction * planet.radius);
      expand_bounds(bounds_min, bounds_max,
                    direction * (planet.radius + terrain_margin));
    }
  }
}

PlanetQuadtreeNode make_node(const PlanetDefinition &planet,
                             const PlanetChunkId &id, int32_t parent) {
  PlanetQuadtreeNode node{};
  node.id = id;
  node.parent = parent;
  node.geometric_error = geometric_error_for_lod(planet, id.lod);
  compute_bounds(planet, id, node.bounds_min, node.bounds_max);
  return node;
}
} // namespace

void PlanetQuadtree::init(const PlanetDefinition &planet, uint32_t max_lod) {
  planet_ = planet;
  max_lod_ = max_lod;
  nodes_.clear();
  face_roots_.fill(-1);
  nodes_.reserve(6);

  for (PlanetFace face : k_faces) {
    PlanetChunkId id{};
    id.face = face;
    id.x = 0;
    id.y = 0;
    id.lod = 0;
    const int32_t index = static_cast<int32_t>(nodes_.size());
    nodes_.push_back(make_node(planet_, id, -1));
    face_roots_[face_index(face)] = index;
  }
}

PlanetQuadtreeNode *PlanetQuadtree::node(int32_t index) {
  if (index < 0 || index >= node_count()) {
    return nullptr;
  }
  return &nodes_[static_cast<size_t>(index)];
}

const PlanetQuadtreeNode *PlanetQuadtree::node(int32_t index) const {
  if (index < 0 || index >= node_count()) {
    return nullptr;
  }
  return &nodes_[static_cast<size_t>(index)];
}

int32_t PlanetQuadtree::root_index(PlanetFace face) const {
  return face_roots_[face_index(face)];
}

bool PlanetQuadtree::subdivide(int32_t node_index) {
  PlanetQuadtreeNode *parent_node = node(node_index);
  if (parent_node == nullptr ||
      static_cast<uint32_t>(parent_node->id.lod) >= max_lod_) {
    return false;
  }

  bool already_subdivided = true;
  for (const int32_t child : parent_node->children) {
    already_subdivided = already_subdivided && child >= 0;
  }
  if (already_subdivided) {
    return true;
  }

  const PlanetChunkId parent_id = parent_node->id;
  const int32_t first_child = static_cast<int32_t>(nodes_.size());
  for (int32_t child_y = 0; child_y < 2; ++child_y) {
    for (int32_t child_x = 0; child_x < 2; ++child_x) {
      PlanetChunkId child_id{};
      child_id.face = parent_id.face;
      child_id.x = parent_id.x * 2 + child_x;
      child_id.y = parent_id.y * 2 + child_y;
      child_id.lod = parent_id.lod + 1;
      nodes_.push_back(make_node(planet_, child_id, node_index));
    }
  }

  parent_node = node(node_index);
  if (parent_node == nullptr) {
    return false;
  }
  for (int32_t i = 0; i < 4; ++i) {
    parent_node->children[static_cast<size_t>(i)] = first_child + i;
  }
  return true;
}
