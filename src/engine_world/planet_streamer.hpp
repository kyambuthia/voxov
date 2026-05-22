#pragma once

#include "engine_render/render_types.hpp"
#include "engine_world/planet_lod.hpp"
#include "engine_world/planet_quadtree.hpp"
#include "engine_world/planet_types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

struct PlanetChunkIdHash {
  size_t operator()(const PlanetChunkId &id) const noexcept;
};

enum class PlanetChunkResidencyState : uint8_t {
  Empty = 0,
  Requested = 1,
  Generating = 2,
  Resident = 3,
};

struct PlanetResidentChunk {
  PlanetChunkId id{};
  PlanetChunkResidencyState state = PlanetChunkResidencyState::Empty;
  RenderMesh mesh{};
  uint64_t mesh_id = 0;
  uint64_t last_requested_frame = 0;
  uint64_t last_visible_frame = 0;
  int32_t node_index = -1;
  bool requested = false;
  bool visible = false;
};

struct PlanetStreamerConfig {
  uint32_t generation_budget_per_update = 8;
  uint32_t max_visible_chunks = 256;
  float lod_error_threshold_pixels = 2.0f;
};

struct PlanetRenderRequest {
  PlanetDefinition planet{};
  uint32_t max_lod = 0;
  glm::dvec3 camera_world_position{0.0};
  glm::mat4 view_projection{1.0f};
  float screen_height_pixels = 1080.0f;
};

class PlanetStreamer {
public:
  PlanetStreamer() = default;

  void init(const PlanetDefinition &planet, uint32_t max_lod);

  void set_config(const PlanetStreamerConfig &config) { config_ = config; }
  const PlanetStreamerConfig &config() const { return config_; }

  void set_generation_budget_per_update(uint32_t budget) {
    config_.generation_budget_per_update = budget;
  }

  void update(const glm::dvec3 &camera_pos, const glm::mat4 &view_projection,
              float screen_height_pixels);
  const std::vector<RenderMesh> &update(const PlanetRenderRequest &request);

  const std::vector<RenderMesh> &render_meshes() const {
    return visible_meshes_;
  }

  size_t streamed_chunk_count() const { return resident_chunks_.size(); }

  bool initialized() const { return initialized_; }
  const PlanetDefinition &planet() const { return planet_; }
  PlanetQuadtree &quadtree() { return quadtree_; }
  const PlanetQuadtree &quadtree() const { return quadtree_; }

  bool is_chunk_resident(const PlanetChunkId &id) const;
  const RenderMesh *resident_mesh(const PlanetChunkId &id) const;
  std::optional<PlanetResidentChunk>
  resident_chunk(const PlanetChunkId &id) const;

  double height_above_base_at_direction(const glm::dvec3 &direction) const;
  double max_height_above_base() const;

  static uint64_t stable_mesh_id(const PlanetChunkId &id);

private:
  using ResidentMap =
      std::unordered_map<PlanetChunkId, PlanetResidentChunk, PlanetChunkIdHash>;

  PlanetResidentChunk &ensure_requested_chunk(const PlanetQuadtreeNode &node,
                                              int32_t node_index);
  void generate_requested_chunks(const std::vector<int32_t> &requested_nodes);
  void rebuild_visible_meshes(const std::vector<int32_t> &visible_nodes);
  void evict_chunks(const std::vector<int32_t> &evict_nodes);

  PlanetDefinition planet_{};
  PlanetQuadtree quadtree_{};
  PlanetLODSelector lod_selector_{};
  PlanetStreamerConfig config_{};
  ResidentMap resident_chunks_{};
  std::vector<RenderMesh> visible_meshes_{};
  uint64_t frame_index_ = 0;
  bool initialized_ = false;
};
