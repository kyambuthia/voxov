#pragma once

#include "engine_world/planet_quadtree.hpp"

#include <glm/glm.hpp>

#include <vector>

struct LODSelectionResult {
  std::vector<int32_t> visible_nodes;
  std::vector<int32_t> new_nodes;
  std::vector<int32_t> evict_nodes;
};

class PlanetLODSelector {
public:
  LODSelectionResult select(PlanetQuadtree &quadtree,
                            const glm::dvec3 &camera_pos,
                            const glm::mat4 &view_projection,
                            float screen_height_pixels,
                            float lod_error_threshold_pixels = 2.0f);

private:
  void select_node(PlanetQuadtree &quadtree, int32_t node_index,
                   const glm::dvec3 &camera_pos,
                   const glm::mat4 &view_projection, float screen_height,
                   float threshold, LODSelectionResult &result);

  float screen_space_error(const PlanetQuadtreeNode &node,
                           const glm::dvec3 &camera_pos, float screen_height,
                           const glm::mat4 &projection) const;

  uint64_t frame_index_ = 0;
  uint64_t last_lod_change_frame_ = 0;
  float hysteresis_factor_ = 1.5f;
};
