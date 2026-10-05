#pragma once

#include <array>
#include <cstdint>
#include <functional>

#include "engine_render/render_types.hpp"

enum class VoxelMaterial : uint8_t {
  Air = 0,
  Dirt = 1,
  Grass = 2,
  Stone = 3,
  // Building materials, placeable from the hotbar. Values are persisted, so
  // only append.
  Sand = 4,
  Planks = 5,
  Brick = 6,
  Snow = 7,
};

inline constexpr VoxelMaterial kLastVoxelMaterial = VoxelMaterial::Snow;

class VoxelChunk {
public:
  static constexpr int CHUNK_X = 64;
  static constexpr int CHUNK_Y = 32;
  static constexpr int CHUNK_Z = 64;
  // Sub-voxel height range exposed by block_height() (0 = empty, 63 = full).
  // Storage quantizes it to 16 levels so the voxel byte can hold 16 materials.
  static constexpr uint8_t kMaxBlockHeight = 63;

  void generate_heightmap_terrain();
  void generate_heightmap_terrain_seeded(uint64_t world_seed, int32_t chunk_x,
                                         int32_t chunk_z);
  void generate_spherical_planet_seeded(uint64_t world_seed);
  void generate_flat_ground(int ground_y);
  VoxelMaterial material(int x, int y, int z) const;
  bool solid(int x, int y, int z) const;
  /// Returns sub-voxel height (0-63), stored as one of 16 levels.
  /// 0 means the block has zero height (air-like), 63 means full block height.
  uint8_t block_height(int x, int y, int z) const;
  void set_material(int x, int y, int z, VoxelMaterial material);
  /// Set material with sub-voxel height (0-63) for Ephilem-style terrain variation.
  /// Height is clamped to [0, 63] and quantized to 16 levels (non-zero stays
  /// non-zero).
  void set_material(int x, int y, int z, VoxelMaterial material, uint8_t height);
  /// true: full-height block keeping its material (Dirt if it was air);
  /// false: air.
  void set_solid(int x, int y, int z, bool value);
  void refresh_surface_materials();

  static glm::vec3 material_color(VoxelMaterial material, bool top_face,
                                   float height_t);

  RenderMesh build_greedy_mesh(const glm::vec3 &origin = glm::vec3(0.0f),
                               float voxel_scale = 1.0f) const;

  // Variant with world-space solid query for inter-chunk face culling.
  // world_solid_at(wx, wy, wz) → true if the voxel is solid in a neighbor chunk.
  RenderMesh build_greedy_mesh(
      const glm::vec3 &origin, float voxel_scale,
      int32_t world_base_x, int32_t world_base_z,
      const std::function<bool(int, int, int)> &world_solid_at) const;
  RenderMesh build_sky_placeholder(float size) const;

private:
  size_t index(int x, int y, int z) const;
  std::array<uint8_t, CHUNK_X * CHUNK_Y * CHUNK_Z> voxels{};
  bool spherical_surface_mode = false;
  glm::vec3 spherical_surface_center = glm::vec3(0.0f);
};
