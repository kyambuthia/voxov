#include "engine_world/planet_lod.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

namespace {
constexpr PlanetFace k_faces[] = {
    PlanetFace::PosX, PlanetFace::NegX, PlanetFace::PosY,
    PlanetFace::NegY, PlanetFace::PosZ, PlanetFace::NegZ,
};

struct Frustum {
  std::array<glm::vec4, 6> planes{};
};

Frustum extract_frustum(const glm::mat4 &vp) {
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

bool aabb_in_frustum(const Frustum &frustum, const glm::vec3 &bmin,
                     const glm::vec3 &bmax) {
  for (const glm::vec4 &plane : frustum.planes) {
    const glm::vec3 positive_vertex(
        plane.x >= 0.0f ? bmax.x : bmin.x,
        plane.y >= 0.0f ? bmax.y : bmin.y,
        plane.z >= 0.0f ? bmax.z : bmin.z);
    if (glm::dot(glm::vec3(plane), positive_vertex) + plane.w < 0.0f) {
      return false;
    }
  }
  return true;
}

bool is_drawable(const PlanetQuadtreeNode &node) {
  return node.state == QuadtreeNodeState::GpuReady ||
         node.state == QuadtreeNodeState::Resident || node.parent < 0;
}

void append_unique(std::vector<int32_t> &values, int32_t value) {
  if (std::find(values.begin(), values.end(), value) == values.end()) {
    values.push_back(value);
  }
}

void mark_needed(PlanetQuadtreeNode &node, int32_t index,
                 LODSelectionResult &result) {
  if (node.state == QuadtreeNodeState::Empty) {
    node.state = QuadtreeNodeState::Requested;
    append_unique(result.new_nodes, index);
  } else if (node.state != QuadtreeNodeState::GpuReady &&
             node.state != QuadtreeNodeState::Resident) {
    append_unique(result.new_nodes, index);
  }
}

bool ranges_touch_or_overlap(int32_t a0, int32_t a1, int32_t b0, int32_t b1) {
  return a0 <= b1 && b0 <= a1;
}

struct NodeGridRect {
  int32_t scale = 1;
  int32_t x0 = 0;
  int32_t y0 = 0;
  int32_t x1 = 0;
  int32_t y1 = 0;
};

NodeGridRect rect_at_common_lod(const PlanetQuadtreeNode &node,
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

bool same_face_adjacent(const PlanetQuadtreeNode &a,
                        const PlanetQuadtreeNode &b) {
  if (a.id.face != b.id.face) {
    return false;
  }

  const int32_t common_lod = std::max(a.id.lod, b.id.lod);
  const NodeGridRect ar = rect_at_common_lod(a, common_lod);
  const NodeGridRect br = rect_at_common_lod(b, common_lod);

  const bool horizontal_touch =
      (ar.x1 + 1 == br.x0 || br.x1 + 1 == ar.x0) &&
      ranges_touch_or_overlap(ar.y0, ar.y1, br.y0, br.y1);
  const bool vertical_touch =
      (ar.y1 + 1 == br.y0 || br.y1 + 1 == ar.y0) &&
      ranges_touch_or_overlap(ar.x0, ar.x1, br.x0, br.x1);
  return horizontal_touch || vertical_touch;
}

void mark_lod_boundary_skirts(PlanetQuadtree &quadtree,
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
          !same_face_adjacent(*a, *b)) {
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
  for (PlanetFace face : k_faces) {
    select_node(quadtree, quadtree.root_index(face), camera_pos, view_projection,
                screen_height_pixels, lod_error_threshold_pixels, result);
  }

  mark_lod_boundary_skirts(quadtree, result.visible_nodes);

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

  const Frustum frustum = extract_frustum(view_projection);
  if (!aabb_in_frustum(frustum, glm::vec3(world_min), glm::vec3(world_max))) {
    return;
  }

  mark_needed(*node, node_index, result);

  const float error_px =
      screen_space_error(*node, camera_pos - quadtree.planet().center,
                         screen_height, view_projection);
  const bool can_subdivide =
      static_cast<uint32_t>(node->id.lod) < quadtree.max_lod();
  const bool wants_finer = can_subdivide && error_px > threshold;

  if (!wants_finer || error_px <= threshold * hysteresis_factor_) {
    append_unique(result.visible_nodes, node_index);
    node->last_used_frame = frame_index_;
    return;
  }

  if (!quadtree.subdivide(node_index)) {
    append_unique(result.visible_nodes, node_index);
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
      mark_needed(*child, child_index, result);
      children_ready = children_ready && is_drawable(*child);
    }
  }

  if (!children_ready) {
    append_unique(result.visible_nodes, node_index);
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
  const double distance =
      std::max(1.0, glm::length(center - camera_pos));

  float focal_scale = std::abs(projection[1][1]);
  if (focal_scale <= 1.0e-5f) {
    focal_scale = 1.0f;
  }

  return node.geometric_error / static_cast<float>(distance) *
         (screen_height * 0.5f * focal_scale);
}
