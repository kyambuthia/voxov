#pragma once

#include <array>
#include <cstdint>

#include "engine_render/render_types.hpp"

enum class VoxelMaterial : uint8_t {
  Air = 0,
  Dirt = 1,
  Grass = 2,
  Stone = 3,
};

class VoxelChunk {
public:
  static constexpr int CHUNK_X = 64;
  static constexpr int CHUNK_Y = 32;
  static constexpr int CHUNK_Z = 64;

  void generate_heightmap_terrain();
  void generate_heightmap_terrain_seeded(uint64_t world_seed, int32_t chunk_x,
                                         int32_t chunk_z);
  void generate_spherical_planet_seeded(uint64_t world_seed);
  void generate_flat_ground(int ground_y);
  VoxelMaterial material(int x, int y, int z) const;
  bool solid(int x, int y, int z) const;
  void set_material(int x, int y, int z, VoxelMaterial material);
  void set_solid(int x, int y, int z, bool value);
  void refresh_surface_materials();

  RenderMesh build_greedy_mesh(const glm::vec3 &origin = glm::vec3(0.0f),
                               float voxel_scale = 1.0f) const;
  RenderMesh build_debug_grid(float span, float step) const;
  RenderMesh build_sky_placeholder(float size) const;

private:
  size_t index(int x, int y, int z) const;
  std::array<uint8_t, CHUNK_X * CHUNK_Y * CHUNK_Z> voxels{};
  bool spherical_surface_mode = false;
  glm::vec3 spherical_surface_center = glm::vec3(0.0f);
};
