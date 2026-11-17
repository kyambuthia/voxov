#pragma once

#include "engine_world/voxel_chunk.hpp"

#include <cstdint>
#include <vector>

// Nibble-packed sky light (high 4 bits, 0..15) and emissive block light
// (low 4 bits, 0..15) per voxel, updated via iterative BFS queues.
class VoxelLightMap {
public:
  VoxelLightMap() = default;
  VoxelLightMap(int size_x, int size_y, int size_z);

  void resize(int size_x, int size_y, int size_z);
  void clear();

  uint8_t sky_light(int x, int y, int z) const;
  uint8_t block_light(int x, int y, int z) const;
  void set_sky_light(int x, int y, int z, uint8_t level);
  void set_block_light(int x, int y, int z, uint8_t level);

  // Seeds sky_light = 15 down open vertical columns and propagates into
  // overhangs/caves via iterative BFS.
  void compute_sunlight(const VoxelChunk &chunk);

  // Iterative BFS block-light addition and removal (with border refill).
  void add_block_light(const VoxelChunk &chunk, int x, int y, int z,
                       uint8_t level);
  void remove_block_light(const VoxelChunk &chunk, int x, int y, int z);

  // Combined normalized brightness in [0.12f, 1.0f].
  float brightness(int x, int y, int z) const;

private:
  bool in_bounds(int x, int y, int z) const;
  size_t index(int x, int y, int z) const;

  int size_x_ = 0;
  int size_y_ = 0;
  int size_z_ = 0;
  std::vector<uint8_t> packed_;
};
