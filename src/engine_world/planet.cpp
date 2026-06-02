//==============================================================================
// Consolidated planet system implementation — all 7 subsystems in one TU.
// Archived for later revisit. See planet.hpp for the public API.
//==============================================================================

#include "engine_world/planet.hpp"

#include "engine_world/voxel_chunk.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

// ═══════════════════════════════════════════════════════════════════════════
//  planet_math
// ═══════════════════════════════════════════════════════════════════════════

namespace {
constexpr double k_math_epsilon = 1.0e-12;

glm::dvec3 math_normalized_or(const glm::dvec3 &value,
                              const glm::dvec3 &fallback) {
  const double len2 = glm::dot(value, value);
  if (len2 <= k_math_epsilon) {
    return fallback;
  }
  return value / std::sqrt(len2);
}
} // namespace

glm::dvec3 face_uv_to_direction(PlanetFace face, double u, double v) {
  glm::dvec3 direction(0.0);
  switch (face) {
  case PlanetFace::PosX:
    direction = glm::dvec3(1.0, v, u);
    break;
  case PlanetFace::NegX:
    direction = glm::dvec3(-1.0, v, -u);
    break;
  case PlanetFace::PosY:
    direction = glm::dvec3(u, 1.0, v);
    break;
  case PlanetFace::NegY:
    direction = glm::dvec3(u, -1.0, -v);
    break;
  case PlanetFace::PosZ:
    direction = glm::dvec3(u, v, 1.0);
    break;
  case PlanetFace::NegZ:
    direction = glm::dvec3(-u, v, -1.0);
    break;
  }
  return math_normalized_or(direction, glm::dvec3(0.0, 1.0, 0.0));
}

PlanetFace direction_to_face(const glm::dvec3 &direction) {
  const glm::dvec3 d =
      math_normalized_or(direction, glm::dvec3(0.0, 1.0, 0.0));
  const glm::dvec3 abs_d = glm::abs(d);

  if (abs_d.x >= abs_d.y && abs_d.x >= abs_d.z) {
    return d.x >= 0.0 ? PlanetFace::PosX : PlanetFace::NegX;
  }
  if (abs_d.y >= abs_d.z) {
    return d.y >= 0.0 ? PlanetFace::PosY : PlanetFace::NegY;
  }
  return d.z >= 0.0 ? PlanetFace::PosZ : PlanetFace::NegZ;
}

PlanetFaceUV direction_to_face_uv(const glm::dvec3 &direction) {
  const glm::dvec3 d =
      math_normalized_or(direction, glm::dvec3(0.0, 1.0, 0.0));
  const PlanetFace face = direction_to_face(d);
  PlanetFaceUV uv{};
  uv.face = face;

  switch (face) {
  case PlanetFace::PosX:
    uv.u = d.z / d.x;
    uv.v = d.y / d.x;
    break;
  case PlanetFace::NegX:
    uv.u = d.z / d.x;
    uv.v = -d.y / d.x;
    break;
  case PlanetFace::PosY:
    uv.u = d.x / d.y;
    uv.v = d.z / d.y;
    break;
  case PlanetFace::NegY:
    uv.u = -d.x / d.y;
    uv.v = d.z / d.y;
    break;
  case PlanetFace::PosZ:
    uv.u = d.x / d.z;
    uv.v = d.y / d.z;
    break;
  case PlanetFace::NegZ:
    uv.u = d.x / d.z;
    uv.v = -d.y / d.z;
    break;
  }

  uv.u = std::clamp(uv.u, -1.0, 1.0);
  uv.v = std::clamp(uv.v, -1.0, 1.0);
  return uv;
}

PlanetChunkId neighbor_chunk_id(const PlanetChunkId &id, int32_t dx,
                                int32_t dy, int32_t chunks_per_face) {
  PlanetChunkId result = id;
  const int32_t max_coord = std::max(0, chunks_per_face - 1);
  result.x = std::clamp(id.x + dx, 0, max_coord);
  result.y = std::clamp(id.y + dy, 0, max_coord);
  return result;
}

glm::dvec3 radial_up(const PlanetDefinition &planet,
                     const glm::dvec3 &world_pos) {
  return math_normalized_or(world_pos - planet.center,
                            glm::dvec3(0.0, 1.0, 0.0));
}

PlanetTangentBasis tangent_basis(const glm::dvec3 &radial_up_value,
                                 const glm::dvec3 &world_up_or_pole_vector) {
  PlanetTangentBasis basis{};
  basis.up =
      math_normalized_or(radial_up_value, glm::dvec3(0.0, 1.0, 0.0));

  glm::dvec3 pole = math_normalized_or(world_up_or_pole_vector,
                                       glm::dvec3(0.0, 1.0, 0.0));
  if (std::abs(glm::dot(basis.up, pole)) > 0.98) {
    pole = glm::dvec3(0.0, 0.0, 1.0);
  }

  basis.east = math_normalized_or(glm::cross(basis.up, pole),
                                  glm::dvec3(1.0, 0.0, 0.0));
  basis.north = math_normalized_or(glm::cross(basis.east, basis.up),
                                   glm::dvec3(0.0, 0.0, 1.0));
  return basis;
}

glm::dvec3 voxel_world_pos(const PlanetDefinition &planet, PlanetFace face,
                           double u, double v, double height_above_base) {
  return planet.center +
         face_uv_to_direction(face, u, v) * (planet.radius + height_above_base);
}

glm::dvec3 local_face_voxel_to_cube_point(
    const PlanetDefinition &planet, const LocalFaceVoxelCoords &coords) {
  const double x = coords.xyz.x;
  const double z = coords.xyz.z;
  const double r = std::max(planet.radius, k_math_epsilon);

  switch (coords.face) {
  case PlanetFace::PosX:
    return glm::dvec3(r, z, x);
  case PlanetFace::NegX:
    return glm::dvec3(-r, z, -x);
  case PlanetFace::PosY:
    return glm::dvec3(x, r, z);
  case PlanetFace::NegY:
    return glm::dvec3(x, -r, -z);
  case PlanetFace::PosZ:
    return glm::dvec3(x, z, r);
  case PlanetFace::NegZ:
    return glm::dvec3(-x, z, -r);
  }
  return glm::dvec3(x, r, z);
}

glm::dvec3 local_face_voxel_to_world_sphere(
    const PlanetDefinition &planet, const LocalFaceVoxelCoords &coords) {
  const glm::dvec3 cube_point =
      local_face_voxel_to_cube_point(planet, coords);
  const glm::dvec3 direction =
      math_normalized_or(cube_point, glm::dvec3(0.0, 1.0, 0.0));
  return planet.center + direction * (planet.radius + coords.xyz.y);
}

LocalFaceVoxelCoords world_sphere_to_local_face_voxel(
    const PlanetDefinition &planet, const glm::dvec3 &world_pos) {
  const glm::dvec3 offset = world_pos - planet.center;
  const double radial_distance = std::sqrt(glm::dot(offset, offset));
  const glm::dvec3 direction =
      math_normalized_or(offset, glm::dvec3(0.0, 1.0, 0.0));
  const PlanetFaceUV face_uv = direction_to_face_uv(direction);

  LocalFaceVoxelCoords coords{};
  coords.face = face_uv.face;
  coords.xyz.x = face_uv.u * planet.radius;
  coords.xyz.y = radial_distance - planet.radius;
  coords.xyz.z = face_uv.v * planet.radius;
  return coords;
}

double cubed_sphere_distortion_factor(const LocalFaceVoxelCoords &coords,
                                      double face_half_extent) {
  constexpr double k_corner_distortion_factor = 1.6180339887498948482;
  const double extent =
      std::max(std::abs(face_half_extent), k_math_epsilon);
  const double u = std::clamp(coords.xyz.x / extent, -1.0, 1.0);
  const double v = std::clamp(coords.xyz.z / extent, -1.0, 1.0);
  const double corner_t = std::clamp((u * u + v * v) * 0.5, 0.0, 1.0);
  return 1.0 + (k_corner_distortion_factor - 1.0) * corner_t;
}

// ═══════════════════════════════════════════════════════════════════════════
//  planet_quadtree
// ═══════════════════════════════════════════════════════════════════════════

namespace {
constexpr PlanetFace k_quadtree_faces[] = {
    PlanetFace::PosX, PlanetFace::NegX, PlanetFace::PosY,
    PlanetFace::NegY, PlanetFace::PosZ, PlanetFace::NegZ,
};

int32_t quadtree_face_index(PlanetFace face) {
  return static_cast<int32_t>(face);
}

double quadtree_lod_cells(int32_t lod) {
  return static_cast<double>(int32_t{1} << std::max(0, lod));
}

void quadtree_node_uv_range(const PlanetChunkId &id, double &u0, double &v0,
                            double &u1, double &v1) {
  const double cells = quadtree_lod_cells(id.lod);
  const double span = 2.0 / cells;
  u0 = -1.0 + span * static_cast<double>(id.x);
  v0 = -1.0 + span * static_cast<double>(id.y);
  u1 = u0 + span;
  v1 = v0 + span;
}

float quadtree_geometric_error_for_lod(const PlanetDefinition &planet,
                                       int32_t lod) {
  const double divisor = quadtree_lod_cells(lod);
  return static_cast<float>((planet.radius * planet.voxel_size) / divisor);
}

void quadtree_expand_bounds(glm::dvec3 &bmin, glm::dvec3 &bmax,
                            const glm::dvec3 &point) {
  bmin = glm::min(bmin, point);
  bmax = glm::max(bmax, point);
}

void quadtree_compute_bounds(const PlanetDefinition &planet,
                             const PlanetChunkId &id, glm::dvec3 &bounds_min,
                             glm::dvec3 &bounds_max) {
  double u0 = 0.0;
  double v0 = 0.0;
  double u1 = 0.0;
  double v1 = 0.0;
  quadtree_node_uv_range(id, u0, v0, u1, v1);

  bounds_min = glm::dvec3(std::numeric_limits<double>::max());
  bounds_max = glm::dvec3(std::numeric_limits<double>::lowest());

  const double um = (u0 + u1) * 0.5;
  const double vm = (v0 + v1) * 0.5;
  const double us[] = {u0, um, u1};
  const double vs[] = {v0, vm, v1};
  const double terrain_margin =
      std::max(planet.voxel_size, planet.voxel_size * 16.0);

  for (const double u : us) {
    for (const double v : vs) {
      const glm::dvec3 direction = face_uv_to_direction(id.face, u, v);
      quadtree_expand_bounds(bounds_min, bounds_max,
                             direction * planet.radius);
      quadtree_expand_bounds(bounds_min, bounds_max,
                             direction * (planet.radius + terrain_margin));
    }
  }
}

PlanetQuadtreeNode quadtree_make_node(const PlanetDefinition &planet,
                                      const PlanetChunkId &id,
                                      int32_t parent) {
  PlanetQuadtreeNode node{};
  node.id = id;
  node.parent = parent;
  node.geometric_error = quadtree_geometric_error_for_lod(planet, id.lod);
  quadtree_compute_bounds(planet, id, node.bounds_min, node.bounds_max);
  return node;
}
} // namespace

void PlanetQuadtree::init(const PlanetDefinition &planet, uint32_t max_lod) {
  planet_ = planet;
  max_lod_ = max_lod;
  nodes_.clear();
  face_roots_.fill(-1);
  nodes_.reserve(6);

  for (PlanetFace face : k_quadtree_faces) {
    PlanetChunkId id{};
    id.face = face;
    id.x = 0;
    id.y = 0;
    id.lod = 0;
    const int32_t index = static_cast<int32_t>(nodes_.size());
    nodes_.push_back(quadtree_make_node(planet_, id, -1));
    face_roots_[quadtree_face_index(face)] = index;
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
  return face_roots_[quadtree_face_index(face)];
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
      nodes_.push_back(quadtree_make_node(planet_, child_id, node_index));
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

// ═══════════════════════════════════════════════════════════════════════════
//  planet_lod
// ═══════════════════════════════════════════════════════════════════════════

namespace {
constexpr PlanetFace k_lod_faces[] = {
    PlanetFace::PosX, PlanetFace::NegX, PlanetFace::PosY,
    PlanetFace::NegY, PlanetFace::PosZ, PlanetFace::NegZ,
};

struct Frustum {
  std::array<glm::vec4, 6> planes{};
};

Frustum lod_extract_frustum(const glm::mat4 &vp) {
  auto row = [&](int i) {
    return glm::vec4(vp[0][i], vp[1][i], vp[2][i], vp[3][i]);
  };

  const glm::vec4 r0 = row(0);
  const glm::vec4 r1 = row(1);
  const glm::vec4 r2 = row(2);
  const glm::vec4 r3 = row(3);

  Frustum frustum{};
  frustum.planes[0] = r3 + r0;
  frustum.planes[1] = r3 - r0;
  frustum.planes[2] = r3 + r1;
  frustum.planes[3] = r3 - r1;
  frustum.planes[4] = r3 + r2;
  frustum.planes[5] = r3 - r2;

  for (glm::vec4 &plane : frustum.planes) {
    const float len = glm::length(glm::vec3(plane));
    if (len > 1.0e-6f) {
      plane /= len;
    }
  }
  return frustum;
}

bool lod_aabb_in_frustum(const Frustum &frustum, const glm::vec3 &bmin,
                         const glm::vec3 &bmax) {
  for (const glm::vec4 &plane : frustum.planes) {
    const glm::vec3 positive_vertex(plane.x >= 0.0f ? bmax.x : bmin.x,
                                    plane.y >= 0.0f ? bmax.y : bmin.y,
                                    plane.z >= 0.0f ? bmax.z : bmin.z);
    if (glm::dot(glm::vec3(plane), positive_vertex) + plane.w < 0.0f) {
      return false;
    }
  }
  return true;
}

bool lod_is_drawable(const PlanetQuadtreeNode &node) {
  return node.state == QuadtreeNodeState::GpuReady ||
         node.state == QuadtreeNodeState::Resident || node.parent < 0;
}

void lod_append_unique(std::vector<int32_t> &values, int32_t value) {
  if (std::find(values.begin(), values.end(), value) == values.end()) {
    values.push_back(value);
  }
}

void lod_mark_needed(PlanetQuadtreeNode &node, int32_t index,
                     LODSelectionResult &result) {
  if (node.state == QuadtreeNodeState::Empty) {
    node.state = QuadtreeNodeState::Requested;
    lod_append_unique(result.new_nodes, index);
  } else if (node.state != QuadtreeNodeState::GpuReady &&
             node.state != QuadtreeNodeState::Resident) {
    lod_append_unique(result.new_nodes, index);
  }
}

bool lod_ranges_touch_or_overlap(int32_t a0, int32_t a1, int32_t b0,
                                  int32_t b1) {
  return a0 <= b1 && b0 <= a1;
}

struct NodeGridRect {
  int32_t scale = 1;
  int32_t x0 = 0;
  int32_t y0 = 0;
  int32_t x1 = 0;
  int32_t y1 = 0;
};

NodeGridRect lod_rect_at_common_lod(const PlanetQuadtreeNode &node,
                                     int32_t common_lod) {
  const int32_t scale = 1 << std::max(0, common_lod - node.id.lod);
  NodeGridRect rect{};
  rect.scale = scale;
  rect.x0 = node.id.x * scale;
  rect.y0 = node.id.y * scale;
  rect.x1 = rect.x0 + scale - 1;
  rect.y1 = rect.y0 + scale - 1;
  return rect;
}

bool lod_same_face_adjacent(const PlanetQuadtreeNode &a,
                            const PlanetQuadtreeNode &b) {
  if (a.id.face != b.id.face) {
    return false;
  }

  const int32_t common_lod = std::max(a.id.lod, b.id.lod);
  const NodeGridRect ar = lod_rect_at_common_lod(a, common_lod);
  const NodeGridRect br = lod_rect_at_common_lod(b, common_lod);

  const bool horizontal_touch =
      (ar.x1 + 1 == br.x0 || br.x1 + 1 == ar.x0) &&
      lod_ranges_touch_or_overlap(ar.y0, ar.y1, br.y0, br.y1);
  const bool vertical_touch =
      (ar.y1 + 1 == br.y0 || br.y1 + 1 == ar.y0) &&
      lod_ranges_touch_or_overlap(ar.x0, ar.x1, br.x0, br.x1);
  return horizontal_touch || vertical_touch;
}

void lod_mark_boundary_skirts(PlanetQuadtree &quadtree,
                              const std::vector<int32_t> &visible_nodes) {
  for (const int32_t index : visible_nodes) {
    if (PlanetQuadtreeNode *node = quadtree.node(index)) {
      node->has_skirts = false;
    }
  }

  for (size_t i = 0; i < visible_nodes.size(); ++i) {
    PlanetQuadtreeNode *a = quadtree.node(visible_nodes[i]);
    if (a == nullptr) {
      continue;
    }
    for (size_t j = i + 1; j < visible_nodes.size(); ++j) {
      PlanetQuadtreeNode *b = quadtree.node(visible_nodes[j]);
      if (b == nullptr || std::abs(a->id.lod - b->id.lod) != 1 ||
          !lod_same_face_adjacent(*a, *b)) {
        continue;
      }

      PlanetQuadtreeNode &coarser = a->id.lod < b->id.lod ? *a : *b;
      coarser.has_skirts = true;
    }
  }
}
} // namespace

LODSelectionResult PlanetLODSelector::select(
    PlanetQuadtree &quadtree, const glm::dvec3 &camera_pos,
    const glm::mat4 &view_projection, float screen_height_pixels,
    float lod_error_threshold_pixels) {
  ++frame_index_;

  LODSelectionResult result{};
  for (PlanetFace face : k_lod_faces) {
    select_node(quadtree, quadtree.root_index(face), camera_pos,
                view_projection, screen_height_pixels,
                lod_error_threshold_pixels, result);
  }

  lod_mark_boundary_skirts(quadtree, result.visible_nodes);

  constexpr uint64_t k_evict_after_frames = 600;
  for (int32_t i = 0; i < quadtree.node_count(); ++i) {
    const PlanetQuadtreeNode *node = quadtree.node(i);
    if (node != nullptr && node->mesh_handle >= 0 &&
        frame_index_ > node->last_used_frame + k_evict_after_frames) {
      result.evict_nodes.push_back(i);
    }
  }

  return result;
}

void PlanetLODSelector::select_node(PlanetQuadtree &quadtree,
                                    int32_t node_index,
                                    const glm::dvec3 &camera_pos,
                                    const glm::mat4 &view_projection,
                                    float screen_height, float threshold,
                                    LODSelectionResult &result) {
  PlanetQuadtreeNode *node = quadtree.node(node_index);
  if (node == nullptr) {
    return;
  }

  const glm::dvec3 world_min = quadtree.planet().center + node->bounds_min;
  const glm::dvec3 world_max = quadtree.planet().center + node->bounds_max;
  node->render_bounds_min = glm::vec3(world_min);
  node->render_bounds_max = glm::vec3(world_max);

  const Frustum frustum = lod_extract_frustum(view_projection);
  if (!lod_aabb_in_frustum(frustum, glm::vec3(world_min),
                           glm::vec3(world_max))) {
    return;
  }

  lod_mark_needed(*node, node_index, result);

  const float error_px =
      screen_space_error(*node, camera_pos - quadtree.planet().center,
                         screen_height, view_projection);
  const bool can_subdivide =
      static_cast<uint32_t>(node->id.lod) < quadtree.max_lod();
  const bool wants_finer = can_subdivide && error_px > threshold;

  if (!wants_finer || error_px <= threshold * hysteresis_factor_) {
    lod_append_unique(result.visible_nodes, node_index);
    node->last_used_frame = frame_index_;
    return;
  }

  if (!quadtree.subdivide(node_index)) {
    lod_append_unique(result.visible_nodes, node_index);
    node->last_used_frame = frame_index_;
    return;
  }

  node = quadtree.node(node_index);
  bool children_ready = node != nullptr;
  if (node != nullptr) {
    for (const int32_t child_index : node->children) {
      PlanetQuadtreeNode *child = quadtree.node(child_index);
      if (child == nullptr) {
        children_ready = false;
        continue;
      }
      lod_mark_needed(*child, child_index, result);
      children_ready = children_ready && lod_is_drawable(*child);
    }
  }

  if (!children_ready) {
    lod_append_unique(result.visible_nodes, node_index);
    if (node != nullptr) {
      node->last_used_frame = frame_index_;
    }
    return;
  }

  last_lod_change_frame_ = frame_index_;
  const std::array<int32_t, 4> child_indices = node->children;
  for (const int32_t child_index : child_indices) {
    select_node(quadtree, child_index, camera_pos, view_projection,
                screen_height, threshold, result);
  }
}

float PlanetLODSelector::screen_space_error(
    const PlanetQuadtreeNode &node, const glm::dvec3 &camera_pos,
    float screen_height, const glm::mat4 &projection) const {
  const glm::dvec3 center =
      (node.bounds_min + node.bounds_max) * 0.5 + glm::dvec3(0.0);
  const double distance = std::max(1.0, glm::length(center - camera_pos));

  float focal_scale = std::abs(projection[1][1]);
  if (focal_scale <= 1.0e-5f) {
    focal_scale = 1.0f;
  }

  return node.geometric_error / static_cast<float>(distance) *
         (screen_height * 0.5f * focal_scale);
}

// ═══════════════════════════════════════════════════════════════════════════
//  planet_terrain
// ═══════════════════════════════════════════════════════════════════════════

namespace {
constexpr int k_planet_chunk_size_x = 16;
constexpr int k_planet_chunk_size_z = 16;
constexpr int k_planet_chunk_height = VoxelChunk::CHUNK_Y;
constexpr int k_planet_max_terrain_height = 30;
constexpr int k_planet_reserved_faces_per_column = 10;
constexpr int k_max_lod_shift = 30;
constexpr double k_mesh_face_edge_epsilon = 1.0e-5;

uint32_t terrain_hash_u32(uint32_t value) {
  value ^= value >> 16u;
  value *= 0x7feb352du;
  value ^= value >> 15u;
  value *= 0x846ca68bu;
  value ^= value >> 16u;
  return value;
}

uint32_t terrain_hash_columns(int32_t x, int32_t z, uint64_t seed,
                              uint32_t salt) {
  uint32_t value = static_cast<uint32_t>(x) * 0x8da6b343u;
  value ^= static_cast<uint32_t>(z) * 0xd8163841u;
  value ^= static_cast<uint32_t>(seed);
  value ^= static_cast<uint32_t>(seed >> 32u) * 0xcb1ab31fu;
  value ^= salt;
  return terrain_hash_u32(value);
}

float terrain_smoothstep(float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

float terrain_lerp(float a, float b, float t) { return a + (b - a) * t; }

float terrain_value_noise(int32_t world_x, int32_t world_z, uint64_t seed,
                          int32_t cell_size, uint32_t salt) {
  const float fx =
      static_cast<float>(world_x) / static_cast<float>(cell_size);
  const float fz =
      static_cast<float>(world_z) / static_cast<float>(cell_size);
  const int32_t x0 = static_cast<int32_t>(std::floor(fx));
  const int32_t z0 = static_cast<int32_t>(std::floor(fz));
  const float tx = terrain_smoothstep(fx - static_cast<float>(x0));
  const float tz = terrain_smoothstep(fz - static_cast<float>(z0));

  auto sample = [&](int32_t x, int32_t z) {
    const uint32_t hash = terrain_hash_columns(x, z, seed, salt);
    return static_cast<float>(hash & 0xffffu) / 32767.5f - 1.0f;
  };

  const float a = terrain_lerp(sample(x0, z0), sample(x0 + 1, z0), tx);
  const float b = terrain_lerp(sample(x0, z0 + 1), sample(x0 + 1, z0 + 1), tx);
  return terrain_lerp(a, b, tz);
}

int terrain_height_fn(int32_t world_x, int32_t world_z, uint64_t seed) {
  float height = 12.0f;
  height += terrain_value_noise(world_x, world_z, seed, 48, 0x6d2b79f5u) * 8.0f;
  height += terrain_value_noise(world_x, world_z, seed, 18, 0x1b56c4e9u) * 4.0f;
  height += terrain_value_noise(world_x, world_z, seed, 7, 0xa511e9b3u) * 1.5f;

  const float plateau =
      terrain_value_noise(world_x, world_z, seed, 64, 0x4f1bbcdu);
  if (plateau > 0.68f) {
    height = terrain_lerp(height, 22.0f,
                          std::clamp((plateau - 0.68f) * 2.5f, 0.0f, 1.0f));
  } else if (plateau < -0.72f) {
    height = terrain_lerp(height, 5.0f,
                          std::clamp((-0.72f - plateau) * 2.0f, 0.0f, 1.0f));
  }

  const int quantized = static_cast<int>(std::round(height));
  return std::clamp(
      quantized, 2,
      std::min(k_planet_max_terrain_height, k_planet_chunk_height - 3));
}

int32_t terrain_configured_columns_per_face(const PlanetDefinition &planet) {
  const int32_t chunks_per_face = std::max(1, planet.chunks_per_face);
  return chunks_per_face * k_planet_chunk_size_x;
}

struct TerrainColumnSample {
  int32_t x = 0;
  int32_t z = 0;
  int height = 0;
};

TerrainColumnSample terrain_sample_column_at_face_uv(
    const PlanetDefinition &planet, double u, double v) {
  const int32_t columns_per_axis = terrain_configured_columns_per_face(planet);
  const double column_x = (std::clamp(u, -1.0, 1.0) + 1.0) * 0.5 *
                          static_cast<double>(columns_per_axis);
  const double column_z = (std::clamp(v, -1.0, 1.0) + 1.0) * 0.5 *
                          static_cast<double>(columns_per_axis);
  TerrainColumnSample sample{};
  sample.x = std::clamp(static_cast<int32_t>(std::floor(column_x)), 0,
                        columns_per_axis - 1);
  sample.z = std::clamp(static_cast<int32_t>(std::floor(column_z)), 0,
                        columns_per_axis - 1);
  sample.height = terrain_height_fn(sample.x, sample.z, planet.seed);
  return sample;
}

int terrain_height_at_face_uv_fn(const PlanetDefinition &planet, double u,
                                 double v) {
  return terrain_sample_column_at_face_uv(planet, u, v).height;
}

glm::dvec2 terrain_chunk_local_column_center_uv(const PlanetChunkUvRange &range,
                                                double local_x,
                                                double local_z) {
  const double tx =
      (local_x + 0.5) / static_cast<double>(k_planet_chunk_size_x);
  const double tz =
      (local_z + 0.5) / static_cast<double>(k_planet_chunk_size_z);
  return glm::dvec2(range.u0 + (range.u1 - range.u0) * tx,
                    range.v0 + (range.v1 - range.v0) * tz);
}

TerrainColumnSample terrain_sample_chunk_local_column(
    const PlanetDefinition &planet, const PlanetChunkUvRange &range,
    double local_x, double local_z) {
  const glm::dvec2 uv =
      terrain_chunk_local_column_center_uv(range, local_x, local_z);
  return terrain_sample_column_at_face_uv(planet, uv.x, uv.y);
}

double terrain_clamp_mesh_face_uv(double value) {
  return std::clamp(value, -1.0 + k_mesh_face_edge_epsilon,
                    1.0 - k_mesh_face_edge_epsilon);
}

VoxelMaterial terrain_column_material(int32_t world_x, int32_t world_z,
                                      int voxel_y, int max_y_in_column,
                                      uint64_t seed) {
  if (voxel_y < max_y_in_column - 5) {
    return VoxelMaterial::Stone;
  }

  const uint32_t hash =
      terrain_hash_columns(world_x, world_z, seed, 0x91e10da5u);
  if (voxel_y == max_y_in_column && max_y_in_column >= 23) {
    return VoxelMaterial::Stone;
  }
  if (voxel_y == max_y_in_column && (hash % 19u) == 0u) {
    return VoxelMaterial::Stone;
  }
  if (voxel_y >= max_y_in_column - 2) {
    return VoxelMaterial::Grass;
  }
  return VoxelMaterial::Dirt;
}

glm::vec3 terrain_color_fn(VoxelMaterial material, int32_t world_x,
                           int32_t world_z, int voxel_y, int max_y_in_column,
                           const glm::ivec3 &face_normal) {
  const float height_t =
      std::clamp(static_cast<float>(voxel_y) /
                     static_cast<float>(k_planet_chunk_height),
                 0.0f, 1.0f);
  glm::vec3 color =
      VoxelChunk::material_color(material, face_normal.y > 0, height_t);
  if (material == VoxelMaterial::Grass && face_normal.y <= 0) {
    color = glm::vec3(0.42f, 0.30f, 0.16f);
  } else if (material == VoxelMaterial::Stone) {
    color = glm::vec3(0.42f + height_t * 0.24f, 0.44f + height_t * 0.22f,
                      0.45f + height_t * 0.18f);
  }

  const uint32_t hash =
      terrain_hash_columns(world_x, world_z, 0x56584f56u, 0xb5297a4du);
  const float column_variation =
      0.82f + static_cast<float>(hash & 0xffu) * (0.28f / 255.0f);
  const float face_light = face_normal.y > 0    ? 1.10f
                           : face_normal.x != 0 ? 0.82f
                                                : 0.92f;
  if (voxel_y == max_y_in_column && ((hash >> 8u) & 7u) == 0u) {
    color += glm::vec3(0.08f, 0.07f, 0.02f);
  }
  return glm::clamp(color * column_variation * face_light, glm::vec3(0.0f),
                    glm::vec3(1.0f));
}

void terrain_clear_chunk(VoxelChunk &chunk) {
  for (int z = 0; z < VoxelChunk::CHUNK_Z; ++z) {
    for (int y = 0; y < VoxelChunk::CHUNK_Y; ++y) {
      for (int x = 0; x < VoxelChunk::CHUNK_X; ++x) {
        chunk.set_material(x, y, z, VoxelMaterial::Air);
      }
    }
  }
}

void terrain_generate_heightfield(VoxelChunk &chunk,
                                  const PlanetDefinition &planet,
                                  const PlanetChunkUvRange &range) {
  terrain_clear_chunk(chunk);

  for (int z = 0; z < k_planet_chunk_size_z; ++z) {
    for (int x = 0; x < k_planet_chunk_size_x; ++x) {
      const TerrainColumnSample sample =
          terrain_sample_chunk_local_column(planet, range, x, z);
      const int max_y = sample.height;
      for (int y = 0; y <= max_y; ++y) {
        chunk.set_material(
            x, y, z,
            terrain_column_material(sample.x, sample.z, y, max_y,
                                    planet.seed));
      }
    }
  }
}

glm::vec3 terrain_remap_local_vertex(
    const PlanetDefinition &planet, const PlanetChunkId &chunk_id,
    const PlanetChunkUvRange &range, const glm::vec3 &local,
    PlanetTerrainRenderMode mode,
    const PlanetSurfaceRenderFrame &surface_frame) {
  const double u = terrain_clamp_mesh_face_uv(
      range.u0 + (range.u1 - range.u0) *
                     (static_cast<double>(local.x) /
                      static_cast<double>(k_planet_chunk_size_x)));
  const double v = terrain_clamp_mesh_face_uv(
      range.v0 + (range.v1 - range.v0) *
                     (static_cast<double>(local.z) /
                      static_cast<double>(k_planet_chunk_size_z)));
  const double height = static_cast<double>(local.y) * planet.voxel_size;

  if (mode == PlanetTerrainRenderMode::SurfaceFlatFace) {
    const int32_t columns_per_face =
        terrain_configured_columns_per_face(planet);
    const int32_t lod = std::clamp(chunk_id.lod, 0, k_max_lod_shift);
    const int32_t cells = int32_t{1} << lod;
    const double cell_columns =
        static_cast<double>(columns_per_face) / static_cast<double>(cells);
    const double local_face_x =
        (static_cast<double>(chunk_id.x) * cell_columns) +
        static_cast<double>(local.x);
    const double local_face_z =
        (static_cast<double>(chunk_id.y) * cell_columns) +
        static_cast<double>(local.z);
    LocalFaceVoxelCoords coords{};
    coords.face = chunk_id.face;
    coords.xyz = glm::dvec3(local_face_x * planet.voxel_size, height,
                            local_face_z * planet.voxel_size);

    const double scale = std::max(surface_frame.distortion_scale, 1.0e-9);
    const glm::dvec3 local_pos =
        (coords.xyz - surface_frame.camera_local_origin) * scale;
    return glm::vec3(local_pos);
  }

  return glm::vec3(
      voxel_world_pos(planet, chunk_id.face, u, v, height));
}

struct VoxelFaceDef {
  glm::ivec3 neighbor;
  std::array<glm::vec3, 4> corners;
};

const VoxelFaceDef k_terrain_voxel_faces[] = {
    {glm::ivec3(1, 0, 0),
     {glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(1.0f, 1.0f, 0.0f),
      glm::vec3(1.0f, 1.0f, 1.0f), glm::vec3(1.0f, 0.0f, 1.0f)}},
    {glm::ivec3(-1, 0, 0),
     {glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f),
      glm::vec3(0.0f, 1.0f, 1.0f), glm::vec3(0.0f, 1.0f, 0.0f)}},
    {glm::ivec3(0, 1, 0),
     {glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.0f, 1.0f, 0.0f),
      glm::vec3(1.0f, 1.0f, 1.0f), glm::vec3(0.0f, 1.0f, 1.0f)}},
    {glm::ivec3(0, -1, 0),
     {glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f),
      glm::vec3(1.0f, 0.0f, 1.0f), glm::vec3(1.0f, 0.0f, 0.0f)}},
    {glm::ivec3(0, 0, 1),
     {glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(1.0f, 0.0f, 1.0f),
      glm::vec3(1.0f, 1.0f, 1.0f), glm::vec3(0.0f, 1.0f, 1.0f)}},
    {glm::ivec3(0, 0, -1),
     {glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f),
      glm::vec3(1.0f, 1.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f)}}};

glm::vec3 terrain_sphere_normal_for_local(const PlanetChunkId &chunk_id,
                                          const PlanetChunkUvRange &range,
                                          const glm::vec3 &local) {
  const double u = terrain_clamp_mesh_face_uv(
      range.u0 + (range.u1 - range.u0) *
                     (static_cast<double>(local.x) /
                      static_cast<double>(k_planet_chunk_size_x)));
  const double v = terrain_clamp_mesh_face_uv(
      range.v0 + (range.v1 - range.v0) *
                     (static_cast<double>(local.z) /
                      static_cast<double>(k_planet_chunk_size_z)));
  return glm::vec3(face_uv_to_direction(chunk_id.face, u, v));
}

void terrain_append_planet_voxel_face(
    RenderMesh &mesh, const PlanetDefinition &planet,
    const PlanetChunkId &chunk_id, const PlanetChunkUvRange &range,
    const glm::ivec3 &voxel, const VoxelFaceDef &face, const glm::vec3 &color,
    PlanetTerrainRenderMode mode,
    const PlanetSurfaceRenderFrame &surface_frame) {
  std::array<glm::vec3, 4> positions{};
  glm::vec3 local_center(0.0f);
  for (size_t i = 0; i < face.corners.size(); ++i) {
    const glm::vec3 local = glm::vec3(voxel) + face.corners[i];
    positions[i] = terrain_remap_local_vertex(planet, chunk_id, range, local,
                                              mode, surface_frame);
    local_center += local;
  }
  local_center *= 0.25f;

  glm::vec3 normal =
      terrain_sphere_normal_for_local(chunk_id, range, local_center);
  if (mode == PlanetTerrainRenderMode::SurfaceFlatFace) {
    normal = glm::vec3(face.neighbor);
  }
  const glm::vec3 expected =
      terrain_remap_local_vertex(
          planet, chunk_id, range,
          local_center + glm::vec3(face.neighbor) * 0.5f, mode,
          surface_frame) -
      terrain_remap_local_vertex(
          planet, chunk_id, range,
          local_center - glm::vec3(face.neighbor) * 0.5f, mode,
          surface_frame);
  const glm::vec3 geometric =
      glm::cross(positions[1] - positions[0], positions[2] - positions[0]);
  const bool flip = glm::dot(geometric, expected) < 0.0f;

  const uint32_t start = static_cast<uint32_t>(mesh.vertices.size());
  for (const glm::vec3 &position : positions) {
    mesh.vertices.push_back({position, color, normal});
  }

  if (!flip) {
    mesh.indices.insert(mesh.indices.end(),
                        {start, start + 1, start + 2, start, start + 2,
                         start + 3});
  } else {
    mesh.indices.insert(mesh.indices.end(),
                        {start, start + 2, start + 1, start, start + 3,
                         start + 2});
  }
}
} // namespace

PlanetChunkUvRange planet_chunk_uv_range(const PlanetChunkId &chunk_id) {
  const int32_t lod = std::clamp(chunk_id.lod, 0, k_max_lod_shift);
  const int32_t cells = int32_t{1} << lod;
  const int32_t cell_x = std::clamp(chunk_id.x, 0, cells - 1);
  const int32_t cell_y = std::clamp(chunk_id.y, 0, cells - 1);
  const double span = 2.0 / static_cast<double>(cells);

  PlanetChunkUvRange range{};
  range.u0 = -1.0 + span * static_cast<double>(cell_x);
  range.v0 = -1.0 + span * static_cast<double>(cell_y);
  range.u1 = range.u0 + span;
  range.v1 = range.v0 + span;
  return range;
}

void stitch_face_edges(VoxelChunk &chunk, PlanetFace face, int32_t chunk_x,
                       int32_t chunk_y, const PlanetDefinition &planet) {
  (void)chunk;
  (void)face;
  (void)chunk_x;
  (void)chunk_y;
  (void)planet;
  // TODO: Implement face-edge stitching in Phase 5b
  return;
}

RenderMesh build_planet_terrain_mesh(
    const PlanetDefinition &planet, const PlanetChunkId &chunk_id,
    PlanetTerrainRenderMode mode,
    const PlanetSurfaceRenderFrame &surface_frame) {
  const PlanetChunkUvRange uv_range = planet_chunk_uv_range(chunk_id);

  VoxelChunk chunk;
  terrain_generate_heightfield(chunk, planet, uv_range);
  stitch_face_edges(chunk, chunk_id.face, chunk_id.x, chunk_id.y, planet);

  RenderMesh mesh{};
  mesh.vertices.reserve(
      static_cast<size_t>(k_planet_chunk_size_x * k_planet_chunk_size_z *
                          k_planet_reserved_faces_per_column * 4));
  mesh.indices.reserve(
      static_cast<size_t>(k_planet_chunk_size_x * k_planet_chunk_size_z *
                          k_planet_reserved_faces_per_column * 6));

  int max_y_per_column[k_planet_chunk_size_x][k_planet_chunk_size_z]{};
  int32_t sample_x_per_column[k_planet_chunk_size_x][k_planet_chunk_size_z]{};
  int32_t sample_z_per_column[k_planet_chunk_size_x][k_planet_chunk_size_z]{};
  for (int z = 0; z < k_planet_chunk_size_z; ++z) {
    for (int x = 0; x < k_planet_chunk_size_x; ++x) {
      const TerrainColumnSample sample =
          terrain_sample_chunk_local_column(planet, uv_range, x, z);
      max_y_per_column[x][z] = sample.height;
      sample_x_per_column[x][z] = sample.x;
      sample_z_per_column[x][z] = sample.z;
    }
  }

  for (int z = 0; z < k_planet_chunk_size_z; ++z) {
    for (int y = 0; y < k_planet_chunk_height; ++y) {
      for (int x = 0; x < k_planet_chunk_size_x; ++x) {
        if (!chunk.solid(x, y, z)) {
          continue;
        }

        const glm::ivec3 voxel(x, y, z);
        const int32_t world_x = sample_x_per_column[x][z];
        const int32_t world_z = sample_z_per_column[x][z];
        const int max_y = max_y_per_column[x][z];
        for (const VoxelFaceDef &face : k_terrain_voxel_faces) {
          const glm::ivec3 neighbor = voxel + face.neighbor;
          bool neighbor_solid =
              chunk.solid(neighbor.x, neighbor.y, neighbor.z);
          if (!neighbor_solid &&
              (neighbor.x < 0 || neighbor.x >= k_planet_chunk_size_x ||
               neighbor.z < 0 || neighbor.z >= k_planet_chunk_size_z) &&
              neighbor.y >= 0 && neighbor.y < k_planet_chunk_height) {
            const glm::dvec2 neighbor_uv =
                terrain_chunk_local_column_center_uv(uv_range, neighbor.x,
                                                     neighbor.z);
            if (neighbor_uv.x >= -1.0 && neighbor_uv.x <= 1.0 &&
                neighbor_uv.y >= -1.0 && neighbor_uv.y <= 1.0) {
              const int neighbor_height = terrain_height_at_face_uv_fn(
                  planet, neighbor_uv.x, neighbor_uv.y);
              neighbor_solid = neighbor.y <= neighbor_height;
            }
          }
          if (neighbor_solid) {
            continue;
          }
          const glm::vec3 color =
              terrain_color_fn(chunk.material(x, y, z), world_x, world_z, y,
                               max_y, face.neighbor);
          terrain_append_planet_voxel_face(mesh, planet, chunk_id, uv_range,
                                           voxel, face, color, mode,
                                           surface_frame);
        }
      }
    }
  }

  mesh.mesh_id = 0x5658504c54455252ull;
  mesh.material = static_cast<uint8_t>(VoxelMaterial::Grass);
  return mesh;
}

RenderMesh build_single_face_planet_terrain_mesh(
    const PlanetDefinition &planet, const PlanetChunkId &chunk_id) {
  return build_planet_terrain_mesh(
      planet, chunk_id, PlanetTerrainRenderMode::SpaceCubedSphere, {});
}

double planet_terrain_max_height_above_base(const PlanetDefinition &planet) {
  const int32_t columns_per_axis =
      terrain_configured_columns_per_face(planet);
  int max_height_voxels = 0;

  for (int32_t z = 0; z < columns_per_axis; ++z) {
    for (int32_t x = 0; x < columns_per_axis; ++x) {
      max_height_voxels = std::max(max_height_voxels,
                                   terrain_height_fn(x, z, planet.seed));
    }
  }

  return static_cast<double>(max_height_voxels + 1) * planet.voxel_size;
}

double planet_terrain_height_above_base_at_direction(
    const PlanetDefinition &planet, const glm::dvec3 &direction) {
  const PlanetFaceUV uv = direction_to_face_uv(direction);
  return static_cast<double>(
             terrain_height_at_face_uv_fn(planet, uv.u, uv.v) + 1) *
         planet.voxel_size;
}

// ═══════════════════════════════════════════════════════════════════════════
//  planet_streamer
// ═══════════════════════════════════════════════════════════════════════════

namespace {
constexpr uint64_t k_streamer_mesh_id_namespace = 0x5658504c00000000ull;
constexpr uint64_t k_streamer_fnv_offset_basis = 14695981039346656037ull;
constexpr uint64_t k_streamer_fnv_prime = 1099511628211ull;

uint64_t streamer_mix_u64(uint64_t value) {
  value ^= value >> 30u;
  value *= 0xbf58476d1ce4e5b9ull;
  value ^= value >> 27u;
  value *= 0x94d049bb133111ebull;
  value ^= value >> 31u;
  return value;
}

uint64_t streamer_chunk_key_bits(const PlanetChunkId &id) {
  uint64_t value = k_streamer_fnv_offset_basis;
  auto append = [&value](uint64_t component) {
    value ^= component;
    value *= k_streamer_fnv_prime;
  };

  append(static_cast<uint8_t>(id.face));
  append(static_cast<uint32_t>(id.x));
  append(static_cast<uint32_t>(id.y));
  append(static_cast<uint32_t>(id.lod));
  return value;
}

bool streamer_same_planet_definition(const PlanetDefinition &a,
                                     const PlanetDefinition &b) {
  return a.center == b.center && a.radius == b.radius &&
         a.voxel_size == b.voxel_size &&
         a.chunks_per_face == b.chunks_per_face && a.seed == b.seed;
}
} // namespace

size_t PlanetChunkIdHash::operator()(const PlanetChunkId &id) const noexcept {
  return static_cast<size_t>(streamer_mix_u64(streamer_chunk_key_bits(id)));
}

void PlanetStreamer::init(const PlanetDefinition &planet, uint32_t max_lod) {
  planet_ = planet;
  quadtree_.init(planet_, max_lod);
  lod_selector_ = PlanetLODSelector{};
  resident_chunks_.clear();
  visible_meshes_.clear();
  visible_mesh_ids_.clear();
  stats_ = PlanetStreamerStats{};
  mesh_set_revision_ = 0;
  frame_index_ = 0;
  initialized_ = true;
}

void PlanetStreamer::update(const glm::dvec3 &camera_pos,
                            const glm::mat4 &view_projection,
                            float screen_height_pixels) {
  if (!initialized_) {
    stats_ = PlanetStreamerStats{};
    return;
  }

  ++frame_index_;
  stats_ = PlanetStreamerStats{};
  for (auto &[id, chunk] : resident_chunks_) {
    (void)id;
    chunk.requested = false;
    chunk.visible = false;
  }

  LODSelectionResult selection = lod_selector_.select(
      quadtree_, camera_pos, view_projection, screen_height_pixels,
      config_.lod_error_threshold_pixels);
  stats_.requested_chunk_count =
      static_cast<uint32_t>(selection.new_nodes.size());

  for (const int32_t node_index : selection.new_nodes) {
    const PlanetQuadtreeNode *node = quadtree_.node(node_index);
    if (node == nullptr) {
      continue;
    }
    ensure_requested_chunk(*node, node_index);
  }

  stats_.generated_chunk_count = generate_requested_chunks(selection.new_nodes);
  rebuild_visible_meshes(selection.visible_nodes);
  stats_.evicted_chunk_count = evict_chunks(selection.evict_nodes);
  stats_.resident_chunk_count =
      static_cast<uint32_t>(resident_chunks_.size());
}

const std::vector<RenderMesh> &
PlanetStreamer::update(const PlanetRenderRequest &request) {
  if (!initialized_ || quadtree_.max_lod() != request.max_lod ||
      !streamer_same_planet_definition(planet_, request.planet)) {
    init(request.planet, request.max_lod);
  }

  update(request.camera_world_position, request.view_projection,
         request.screen_height_pixels);
  return visible_meshes_;
}

bool PlanetStreamer::is_chunk_resident(const PlanetChunkId &id) const {
  const auto it = resident_chunks_.find(id);
  return it != resident_chunks_.end() &&
         it->second.state == PlanetChunkResidencyState::Resident;
}

const RenderMesh *PlanetStreamer::resident_mesh(
    const PlanetChunkId &id) const {
  const auto it = resident_chunks_.find(id);
  if (it == resident_chunks_.end() ||
      it->second.state != PlanetChunkResidencyState::Resident) {
    return nullptr;
  }
  return &it->second.mesh;
}

std::optional<PlanetResidentChunk>
PlanetStreamer::resident_chunk(const PlanetChunkId &id) const {
  const auto it = resident_chunks_.find(id);
  if (it == resident_chunks_.end()) {
    return std::nullopt;
  }
  return it->second;
}

double PlanetStreamer::height_above_base_at_direction(
    const glm::dvec3 &direction) const {
  return planet_terrain_height_above_base_at_direction(planet_, direction);
}

double PlanetStreamer::max_height_above_base() const {
  return planet_terrain_max_height_above_base(planet_);
}

uint64_t PlanetStreamer::stable_mesh_id(const PlanetChunkId &id) {
  const uint64_t mixed =
      streamer_mix_u64(streamer_chunk_key_bits(id));
  const uint64_t mesh_id =
      k_streamer_mesh_id_namespace ^ (mixed & 0x0000ffffffffffffull);
  return mesh_id == 0 ? k_streamer_mesh_id_namespace : mesh_id;
}

PlanetResidentChunk &
PlanetStreamer::ensure_requested_chunk(const PlanetQuadtreeNode &node,
                                       int32_t node_index) {
  PlanetResidentChunk &chunk = resident_chunks_[node.id];
  if (chunk.state == PlanetChunkResidencyState::Empty) {
    chunk.id = node.id;
    chunk.mesh_id = stable_mesh_id(node.id);
    chunk.state = PlanetChunkResidencyState::Requested;
  }

  chunk.node_index = node_index;
  chunk.requested = true;
  chunk.last_requested_frame = frame_index_;
  return chunk;
}

uint32_t PlanetStreamer::generate_requested_chunks(
    const std::vector<int32_t> &requested_nodes) {
  uint32_t generated = 0;
  for (const int32_t node_index : requested_nodes) {
    if (generated >= config_.generation_budget_per_update) {
      break;
    }

    PlanetQuadtreeNode *node = quadtree_.node(node_index);
    if (node == nullptr) {
      continue;
    }

    PlanetResidentChunk &chunk = ensure_requested_chunk(*node, node_index);
    if (chunk.state == PlanetChunkResidencyState::Resident ||
        chunk.state == PlanetChunkResidencyState::Generating) {
      continue;
    }

    chunk.state = PlanetChunkResidencyState::Generating;
    node->state = QuadtreeNodeState::Generating;

    RenderMesh mesh =
        build_single_face_planet_terrain_mesh(planet_, node->id);
    mesh.mesh_id = chunk.mesh_id;

    chunk.mesh = std::move(mesh);
    chunk.state = PlanetChunkResidencyState::Resident;
    chunk.last_visible_frame = frame_index_;

    node->state = QuadtreeNodeState::Resident;
    node->mesh_handle = static_cast<int32_t>(chunk.mesh_id & 0x7fffffffu);
    ++generated;
  }
  return generated;
}

void PlanetStreamer::rebuild_visible_meshes(
    const std::vector<int32_t> &visible_nodes) {
  visible_meshes_.clear();
  const size_t max_visible = static_cast<size_t>(config_.max_visible_chunks);
  visible_meshes_.reserve(std::min(visible_nodes.size(), max_visible));
  std::vector<uint64_t> next_visible_mesh_ids;
  next_visible_mesh_ids.reserve(std::min(visible_nodes.size(), max_visible));

  for (const int32_t node_index : visible_nodes) {
    if (visible_meshes_.size() >= max_visible) {
      break;
    }

    PlanetQuadtreeNode *node = quadtree_.node(node_index);
    if (node == nullptr) {
      continue;
    }

    auto it = resident_chunks_.find(node->id);
    if (it == resident_chunks_.end() ||
        it->second.state != PlanetChunkResidencyState::Resident) {
      continue;
    }

    PlanetResidentChunk &chunk = it->second;
    chunk.visible = true;
    chunk.last_visible_frame = frame_index_;
    node->state = QuadtreeNodeState::Resident;
    node->last_used_frame = frame_index_;
    visible_meshes_.push_back(chunk.mesh);
    next_visible_mesh_ids.push_back(chunk.mesh_id);
  }

  stats_.visible_chunk_count =
      static_cast<uint32_t>(visible_meshes_.size());
  if (next_visible_mesh_ids != visible_mesh_ids_) {
    visible_mesh_ids_ = std::move(next_visible_mesh_ids);
    ++mesh_set_revision_;
  }
}

uint32_t PlanetStreamer::evict_chunks(const std::vector<int32_t> &evict_nodes) {
  uint32_t evicted = 0;
  for (const int32_t node_index : evict_nodes) {
    PlanetQuadtreeNode *node = quadtree_.node(node_index);
    if (node == nullptr) {
      continue;
    }

    auto it = resident_chunks_.find(node->id);
    if (it == resident_chunks_.end() || it->second.visible) {
      continue;
    }

    resident_chunks_.erase(it);
    node->state = QuadtreeNodeState::Empty;
    node->mesh_handle = -1;
    ++evicted;
  }
  return evicted;
}

// ═══════════════════════════════════════════════════════════════════════════
//  atmosphere_transition_manager
// ═══════════════════════════════════════════════════════════════════════════

namespace {
double atm_smoothstep(double t) {
  t = std::clamp(t, 0.0, 1.0);
  return t * t * (3.0 - 2.0 * t);
}
} // namespace

void AtmosphereTransitionManager::configure(
    const AtmosphereTransitionConfig &config) {
  config_ = config;
  if (config_.space_altitude < config_.surface_altitude) {
    std::swap(config_.space_altitude, config_.surface_altitude);
  }
  config_.fade_seconds = std::max(config_.fade_seconds, 0.001);
}

void AtmosphereTransitionManager::reset_to_space() {
  snapshot_ = AtmosphereTransitionSnapshot{};
  snapshot_.state = PlanetRenderState::Space;
  snapshot_.space_alpha = 1.0;
  snapshot_.surface_alpha = 0.0;
  snapshot_.velocity_frozen = false;
  snapshot_.surface_physics_active = false;
  transition_t_ = 0.0;
}

void AtmosphereTransitionManager::reset_to_surface(
    const PlanetDefinition &planet, const glm::dvec3 &world_position) {
  snapshot_ = AtmosphereTransitionSnapshot{};
  snapshot_.state = PlanetRenderState::Surface;
  snapshot_.space_alpha = 0.0;
  snapshot_.surface_alpha = 1.0;
  snapshot_.velocity_frozen = false;
  snapshot_.surface_physics_active = true;
  refresh_landing_metrics(planet, world_position);
  transition_t_ = 1.0;
}

void AtmosphereTransitionManager::update(const PlanetDefinition &planet,
                                         const glm::dvec3 &world_position,
                                         const glm::dvec3 &velocity,
                                         double dt) {
  const double altitude =
      glm::length(world_position - planet.center) - planet.radius;

  switch (snapshot_.state) {
  case PlanetRenderState::Space:
    if (altitude <= config_.surface_altitude) {
      begin_descent(planet, world_position, velocity);
    }
    break;
  case PlanetRenderState::Surface:
    refresh_landing_metrics(planet, world_position);
    if (altitude >= config_.space_altitude) {
      begin_ascent(planet, world_position, velocity);
    }
    break;
  case PlanetRenderState::Descending:
    refresh_landing_metrics(planet, world_position);
    transition_t_ += dt / config_.fade_seconds;
    apply_transition_alpha(true);
    if (transition_t_ >= 1.0) {
      finish_surface();
    }
    break;
  case PlanetRenderState::Ascending:
    refresh_landing_metrics(planet, world_position);
    transition_t_ += dt / config_.fade_seconds;
    apply_transition_alpha(false);
    if (transition_t_ >= 1.0) {
      finish_space();
    }
    break;
  }
}

void AtmosphereTransitionManager::begin_descent(
    const PlanetDefinition &planet, const glm::dvec3 &world_position,
    const glm::dvec3 &velocity) {
  snapshot_.state = PlanetRenderState::Descending;
  snapshot_.frozen_velocity = velocity;
  snapshot_.velocity_frozen = true;
  snapshot_.surface_physics_active = false;
  transition_t_ = 0.0;
  refresh_landing_metrics(planet, world_position);
  apply_transition_alpha(true);
}

void AtmosphereTransitionManager::begin_ascent(
    const PlanetDefinition &planet, const glm::dvec3 &world_position,
    const glm::dvec3 &velocity) {
  snapshot_.state = PlanetRenderState::Ascending;
  snapshot_.frozen_velocity = velocity;
  snapshot_.velocity_frozen = true;
  snapshot_.surface_physics_active = false;
  transition_t_ = 0.0;
  refresh_landing_metrics(planet, world_position);
  apply_transition_alpha(false);
}

void AtmosphereTransitionManager::finish_surface() {
  snapshot_.state = PlanetRenderState::Surface;
  snapshot_.space_alpha = 0.0;
  snapshot_.surface_alpha = 1.0;
  snapshot_.velocity_frozen = false;
  snapshot_.surface_physics_active = true;
  transition_t_ = 1.0;
}

void AtmosphereTransitionManager::finish_space() {
  snapshot_.state = PlanetRenderState::Space;
  snapshot_.space_alpha = 1.0;
  snapshot_.surface_alpha = 0.0;
  snapshot_.velocity_frozen = false;
  snapshot_.surface_physics_active = false;
  transition_t_ = 1.0;
}

void AtmosphereTransitionManager::refresh_landing_metrics(
    const PlanetDefinition &planet, const glm::dvec3 &world_position) {
  snapshot_.player_local =
      world_sphere_to_local_face_voxel(planet, world_position);
  snapshot_.active_face = snapshot_.player_local.face;
  snapshot_.distortion_scale =
      cubed_sphere_distortion_factor(snapshot_.player_local, planet.radius);
}

void AtmosphereTransitionManager::apply_transition_alpha(bool surface_in) {
  const double a = atm_smoothstep(transition_t_);
  if (surface_in) {
    snapshot_.space_alpha = 1.0 - a;
    snapshot_.surface_alpha = a;
  } else {
    snapshot_.space_alpha = a;
    snapshot_.surface_alpha = 1.0 - a;
  }
}

// ═══════════════════════════════════════════════════════════════════════════
//  planet_debug
// ═══════════════════════════════════════════════════════════════════════════

namespace {
constexpr PlanetFace k_debug_faces[] = {
    PlanetFace::PosX, PlanetFace::NegX, PlanetFace::PosY,
    PlanetFace::NegY, PlanetFace::PosZ, PlanetFace::NegZ,
};

glm::vec3 debug_face_color(PlanetFace face) {
  switch (face) {
  case PlanetFace::PosX:
    return glm::vec3(0.95f, 0.12f, 0.10f);
  case PlanetFace::NegX:
    return glm::vec3(0.40f, 0.04f, 0.035f);
  case PlanetFace::PosY:
    return glm::vec3(0.12f, 0.82f, 0.18f);
  case PlanetFace::NegY:
    return glm::vec3(0.035f, 0.34f, 0.08f);
  case PlanetFace::PosZ:
    return glm::vec3(0.12f, 0.34f, 1.0f);
  case PlanetFace::NegZ:
    return glm::vec3(0.035f, 0.08f, 0.42f);
  }
  return glm::vec3(1.0f);
}

glm::vec3 debug_sphere_point(const PlanetDefinition &planet, PlanetFace face,
                             double u, double v, double radius_offset = 0.0) {
  const glm::dvec3 direction = face_uv_to_direction(face, u, v);
  return glm::vec3(planet.center +
                   direction * (planet.radius + radius_offset));
}

RenderVertex debug_vertex_fn(const PlanetDefinition &planet, PlanetFace face,
                             double u, double v, const glm::vec3 &color,
                             double radius_offset = 0.0) {
  const glm::dvec3 direction = face_uv_to_direction(face, u, v);
  return RenderVertex{
      glm::vec3(planet.center + direction * (planet.radius + radius_offset)),
      color,
      glm::vec3(direction),
  };
}

void debug_append_oriented_quad(RenderMesh &mesh, const RenderVertex &a,
                                const RenderVertex &b, const RenderVertex &c,
                                const RenderVertex &d) {
  const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
  mesh.vertices.push_back(a);
  mesh.vertices.push_back(b);
  mesh.vertices.push_back(c);
  mesh.vertices.push_back(d);

  const glm::vec3 radial =
      glm::normalize(a.normal + b.normal + c.normal + d.normal);
  const glm::vec3 tri_normal = glm::normalize(
      glm::cross(b.position - a.position, c.position - a.position));
  if (glm::dot(tri_normal, radial) >= 0.0f) {
    mesh.indices.insert(mesh.indices.end(),
                        {base, base + 1, base + 2, base, base + 2, base + 3});
  } else {
    mesh.indices.insert(mesh.indices.end(),
                        {base, base + 2, base + 1, base, base + 3, base + 2});
  }
}

void debug_append_line_box(RenderMesh &mesh, const glm::vec3 &center,
                           const glm::vec3 &start, const glm::vec3 &end,
                           float half_width, const glm::vec3 &color) {
  const glm::vec3 delta = end - start;
  const float len = glm::length(delta);
  if (len <= 1.0e-5f) {
    return;
  }

  const glm::vec3 dir = delta / len;
  glm::vec3 radial = (start + end) * 0.5f - center;
  if (glm::length(radial) <= 1.0e-5f) {
    radial = glm::vec3(0.0f, 1.0f, 0.0f);
  }
  glm::vec3 side = glm::cross(dir, glm::normalize(radial));
  if (glm::length(side) <= 1.0e-5f) {
    side = glm::cross(dir, glm::vec3(0.0f, 1.0f, 0.0f));
  }
  if (glm::length(side) <= 1.0e-5f) {
    side = glm::cross(dir, glm::vec3(1.0f, 0.0f, 0.0f));
  }
  side = glm::normalize(side) * half_width;
  const glm::vec3 up = glm::normalize(glm::cross(side, dir)) * half_width;

  const glm::vec3 a = start - side - up;
  const glm::vec3 b = start + side - up;
  const glm::vec3 c = start + side + up;
  const glm::vec3 d = start - side + up;
  const glm::vec3 e = end - side - up;
  const glm::vec3 f = end + side - up;
  const glm::vec3 g = end + side + up;
  const glm::vec3 h = end - side + up;

  auto quad = [&](const glm::vec3 &p0, const glm::vec3 &p1,
                  const glm::vec3 &p2, const glm::vec3 &p3) {
    const glm::vec3 normal =
        glm::normalize(glm::cross(p1 - p0, p2 - p0));
    debug_append_oriented_quad(mesh, {p0, color, normal},
                               {p1, color, normal}, {p2, color, normal},
                               {p3, color, normal});
  };

  quad(a, b, f, e);
  quad(d, h, g, c);
  quad(a, e, h, d);
  quad(b, c, g, f);
  quad(a, d, c, b);
  quad(e, f, g, h);
}

void debug_append_face_grid(RenderMesh &mesh, const PlanetDefinition &planet,
                            PlanetFace face, int32_t subdivisions,
                            float line_thickness, const glm::vec3 &color) {
  const int32_t clamped = std::max(1, subdivisions);
  const int32_t segments = std::max(4, clamped * 4);
  const double radius_offset = static_cast<double>(line_thickness) * 1.5;

  auto uv = [clamped](int32_t i) {
    return -1.0 + 2.0 * static_cast<double>(i) / static_cast<double>(clamped);
  };

  for (int32_t i = 0; i <= clamped; ++i) {
    const double fixed_u = uv(i);
    const double fixed_v = uv(i);
    for (int32_t s = 0; s < segments; ++s) {
      const double t0 = -1.0 + 2.0 * static_cast<double>(s) /
                                  static_cast<double>(segments);
      const double t1 = -1.0 + 2.0 * static_cast<double>(s + 1) /
                                  static_cast<double>(segments);
      debug_append_line_box(
          mesh, glm::vec3(planet.center),
          debug_sphere_point(planet, face, fixed_u, t0, radius_offset),
          debug_sphere_point(planet, face, fixed_u, t1, radius_offset),
          line_thickness, color);
      debug_append_line_box(
          mesh, glm::vec3(planet.center),
          debug_sphere_point(planet, face, t0, fixed_v, radius_offset),
          debug_sphere_point(planet, face, t1, fixed_v, radius_offset),
          line_thickness, color);
    }
  }
}
} // namespace

RenderMesh build_debug_planet_mesh(const PlanetDefinition &planet,
                                   int32_t grid_size) {
  RenderMesh mesh{};
  const int32_t clamped_grid = std::max(1, grid_size);
  mesh.vertices.reserve(
      static_cast<size_t>(6 * clamped_grid * clamped_grid * 4));
  mesh.indices.reserve(
      static_cast<size_t>(6 * clamped_grid * clamped_grid * 6));

  for (PlanetFace face : k_debug_faces) {
    const glm::vec3 color = debug_face_color(face);
    for (int32_t y = 0; y < clamped_grid; ++y) {
      const double v0 = -1.0 + 2.0 * static_cast<double>(y) /
                                  static_cast<double>(clamped_grid);
      const double v1 = -1.0 + 2.0 * static_cast<double>(y + 1) /
                                  static_cast<double>(clamped_grid);
      for (int32_t x = 0; x < clamped_grid; ++x) {
        const double u0 = -1.0 + 2.0 * static_cast<double>(x) /
                                    static_cast<double>(clamped_grid);
        const double u1 = -1.0 + 2.0 * static_cast<double>(x + 1) /
                                    static_cast<double>(clamped_grid);
        debug_append_oriented_quad(
            mesh, debug_vertex_fn(planet, face, u0, v0, color),
            debug_vertex_fn(planet, face, u1, v0, color),
            debug_vertex_fn(planet, face, u1, v1, color),
            debug_vertex_fn(planet, face, u0, v1, color));
      }
    }
  }
  return mesh;
}

RenderMesh build_planet_impostor_mesh(const PlanetDefinition &planet,
                                      int subdivisions) {
  (void)subdivisions;
  return build_debug_planet_mesh(planet, 6);
}

RenderMesh build_debug_planet_grid_mesh(const PlanetDefinition &planet,
                                        int32_t subdivisions,
                                        float line_thickness) {
  RenderMesh mesh{};
  const glm::vec3 color(0.98f, 0.98f, 0.90f);
  for (PlanetFace face : k_debug_faces) {
    debug_append_face_grid(mesh, planet, face, subdivisions, line_thickness,
                           color);
  }
  return mesh;
}

RenderMesh build_debug_planet_face_highlight_mesh(
    const PlanetDefinition &planet, PlanetFace face, float line_thickness) {
  RenderMesh mesh{};
  debug_append_face_grid(mesh, planet, face, 1, line_thickness,
                         glm::vec3(1.0f, 0.94f, 0.12f));
  return mesh;
}

PlanetFace debug_planet_camera_face(const PlanetDefinition &planet,
                                    const glm::dvec3 &camera_position,
                                    const glm::dvec3 &camera_forward) {
  if (glm::dot(camera_forward, camera_forward) <= 1.0e-12) {
    return PlanetFace::PosZ;
  }
  const glm::dvec3 dir = glm::normalize(camera_forward);
  const glm::dvec3 origin_to_center = camera_position - planet.center;
  const double b = 2.0 * glm::dot(origin_to_center, dir);
  const double c = glm::dot(origin_to_center, origin_to_center) -
                   planet.radius * planet.radius;
  const double discriminant = b * b - 4.0 * c;
  if (discriminant >= 0.0) {
    const double root = std::sqrt(discriminant);
    const double t0 = (-b - root) * 0.5;
    const double t1 = (-b + root) * 0.5;
    const double t = t0 >= 0.0 ? t0 : t1;
    if (t >= 0.0) {
      return direction_to_face(camera_position + dir * t - planet.center);
    }
  }
  return direction_to_face(dir);
}

const char *planet_face_debug_name(PlanetFace face) {
  switch (face) {
  case PlanetFace::PosX:
    return "+X";
  case PlanetFace::NegX:
    return "-X";
  case PlanetFace::PosY:
    return "+Y";
  case PlanetFace::NegY:
    return "-Y";
  case PlanetFace::PosZ:
    return "+Z";
  case PlanetFace::NegZ:
    return "-Z";
  }
  return "?";
}
