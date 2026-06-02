#pragma once
//==============================================================================
// Consolidated planet system header — archived for later revisit.
// Bundles: types, math, quadtree, LOD, streamer, terrain, atmosphere, debug.
//==============================================================================

#include "engine_render/render_types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

// ── planet_types ───────────────────────────────────────────────────────────

enum class PlanetFace : uint8_t {
  PosX = 0,
  NegX = 1,
  PosY = 2,
  NegY = 3,
  PosZ = 4,
  NegZ = 5,
};

struct PlanetDefinition {
  glm::dvec3 center{0.0};
  double radius = 1024.0;
  double voxel_size = 1.0;
  int32_t chunks_per_face = 64;
  uint64_t seed = 0;
};

struct PlanetChunkId {
  PlanetFace face = PlanetFace::PosY;
  int32_t x = 0;
  int32_t y = 0;
  int32_t lod = 0;

  friend bool operator==(const PlanetChunkId &a,
                         const PlanetChunkId &b) = default;
};

struct PlanetFaceUV {
  PlanetFace face = PlanetFace::PosY;
  double u = 0.0;
  double v = 0.0;
};

struct LocalFaceVoxelCoords {
  PlanetFace face = PlanetFace::PosY;
  glm::dvec3 xyz{0.0};
};

struct PlanetTangentBasis {
  glm::dvec3 east{1.0, 0.0, 0.0};
  glm::dvec3 north{0.0, 0.0, 1.0};
  glm::dvec3 up{0.0, 1.0, 0.0};
};

// ── planet_math ────────────────────────────────────────────────────────────

glm::dvec3 face_uv_to_direction(PlanetFace face, double u, double v);
PlanetFace direction_to_face(const glm::dvec3 &direction);
PlanetFaceUV direction_to_face_uv(const glm::dvec3 &direction);
PlanetChunkId neighbor_chunk_id(const PlanetChunkId &id, int32_t dx,
                                int32_t dy, int32_t chunks_per_face);
glm::dvec3 radial_up(const PlanetDefinition &planet,
                     const glm::dvec3 &world_pos);
PlanetTangentBasis tangent_basis(
    const glm::dvec3 &radial_up,
    const glm::dvec3 &world_up_or_pole_vector = glm::dvec3(0.0, 1.0, 0.0));
glm::dvec3 voxel_world_pos(const PlanetDefinition &planet, PlanetFace face,
                           double u, double v, double height_above_base);
glm::dvec3 local_face_voxel_to_cube_point(const PlanetDefinition &planet,
                                          const LocalFaceVoxelCoords &coords);
glm::dvec3 local_face_voxel_to_world_sphere(
    const PlanetDefinition &planet, const LocalFaceVoxelCoords &coords);
LocalFaceVoxelCoords world_sphere_to_local_face_voxel(
    const PlanetDefinition &planet, const glm::dvec3 &world_pos);
double cubed_sphere_distortion_factor(const LocalFaceVoxelCoords &coords,
                                      double face_half_extent);

// ── planet_quadtree ────────────────────────────────────────────────────────

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

// ── planet_lod ─────────────────────────────────────────────────────────────

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

// ── planet_terrain ─────────────────────────────────────────────────────────

class VoxelChunk;

struct PlanetChunkUvRange {
  double u0 = -1.0;
  double v0 = -1.0;
  double u1 = 1.0;
  double v1 = 1.0;
};

enum class PlanetTerrainRenderMode {
  SpaceCubedSphere = 0,
  SurfaceFlatFace = 1,
};

struct PlanetSurfaceRenderFrame {
  PlanetFace face = PlanetFace::PosY;
  glm::dvec3 camera_local_origin{0.0};
  double distortion_scale = 1.0;
};

PlanetChunkUvRange planet_chunk_uv_range(const PlanetChunkId &chunk_id);

RenderMesh build_single_face_planet_terrain_mesh(
    const PlanetDefinition &planet, const PlanetChunkId &chunk_id);
RenderMesh build_planet_terrain_mesh(
    const PlanetDefinition &planet, const PlanetChunkId &chunk_id,
    PlanetTerrainRenderMode mode,
    const PlanetSurfaceRenderFrame &surface_frame = {});

double planet_terrain_max_height_above_base(const PlanetDefinition &planet);
double planet_terrain_height_above_base_at_direction(
    const PlanetDefinition &planet, const glm::dvec3 &direction);

void stitch_face_edges(VoxelChunk &chunk, PlanetFace face, int32_t chunk_x,
                       int32_t chunk_y, const PlanetDefinition &planet);

// ── planet_streamer ────────────────────────────────────────────────────────

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

struct PlanetStreamerStats {
  uint32_t requested_chunk_count = 0;
  uint32_t resident_chunk_count = 0;
  uint32_t visible_chunk_count = 0;
  uint32_t generated_chunk_count = 0;
  uint32_t evicted_chunk_count = 0;
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
  const PlanetStreamerStats &stats() const { return stats_; }
  uint64_t mesh_set_revision() const { return mesh_set_revision_; }

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
  uint32_t
  generate_requested_chunks(const std::vector<int32_t> &requested_nodes);
  void rebuild_visible_meshes(const std::vector<int32_t> &visible_nodes);
  uint32_t evict_chunks(const std::vector<int32_t> &evict_nodes);

  PlanetDefinition planet_{};
  PlanetQuadtree quadtree_{};
  PlanetLODSelector lod_selector_{};
  PlanetStreamerConfig config_{};
  ResidentMap resident_chunks_{};
  std::vector<RenderMesh> visible_meshes_{};
  std::vector<uint64_t> visible_mesh_ids_{};
  PlanetStreamerStats stats_{};
  uint64_t mesh_set_revision_ = 0;
  uint64_t frame_index_ = 0;
  bool initialized_ = false;
};

// ── atmosphere_transition ──────────────────────────────────────────────────

enum class PlanetRenderState {
  Space = 0,
  Descending = 1,
  Surface = 2,
  Ascending = 3,
};

struct AtmosphereTransitionConfig {
  double surface_altitude = 96.0;
  double space_altitude = 192.0;
  double fade_seconds = 1.25;
};

struct AtmosphereTransitionSnapshot {
  PlanetRenderState state = PlanetRenderState::Space;
  PlanetFace active_face = PlanetFace::PosY;
  LocalFaceVoxelCoords player_local{};
  glm::dvec3 frozen_velocity{0.0};
  double space_alpha = 1.0;
  double surface_alpha = 0.0;
  double distortion_scale = 1.0;
  bool velocity_frozen = false;
  bool surface_physics_active = false;
};

class AtmosphereTransitionManager {
public:
  void configure(const AtmosphereTransitionConfig &config);
  void reset_to_space();
  void reset_to_surface(const PlanetDefinition &planet,
                        const glm::dvec3 &world_position);

  void update(const PlanetDefinition &planet, const glm::dvec3 &world_position,
              const glm::dvec3 &velocity, double dt);

  const AtmosphereTransitionSnapshot &snapshot() const { return snapshot_; }

private:
  void begin_descent(const PlanetDefinition &planet,
                     const glm::dvec3 &world_position,
                     const glm::dvec3 &velocity);
  void begin_ascent(const PlanetDefinition &planet,
                    const glm::dvec3 &world_position,
                    const glm::dvec3 &velocity);
  void finish_surface();
  void finish_space();
  void refresh_landing_metrics(const PlanetDefinition &planet,
                               const glm::dvec3 &world_position);
  void apply_transition_alpha(bool surface_in);

  AtmosphereTransitionConfig config_{};
  AtmosphereTransitionSnapshot snapshot_{};
  double transition_t_ = 0.0;
};

// ── planet_debug ───────────────────────────────────────────────────────────

RenderMesh build_debug_planet_mesh(const PlanetDefinition &planet,
                                   int32_t grid_size);
RenderMesh build_planet_impostor_mesh(const PlanetDefinition &planet,
                                      int subdivisions);
RenderMesh build_debug_planet_grid_mesh(const PlanetDefinition &planet,
                                        int32_t subdivisions,
                                        float line_thickness);
RenderMesh build_debug_planet_face_highlight_mesh(
    const PlanetDefinition &planet, PlanetFace face, float line_thickness);
PlanetFace debug_planet_camera_face(const PlanetDefinition &planet,
                                    const glm::dvec3 &camera_position,
                                    const glm::dvec3 &camera_forward);
const char *planet_face_debug_name(PlanetFace face);
