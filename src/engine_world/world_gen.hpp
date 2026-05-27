#pragma once

#include <cstdint>

class VoxelChunk;

struct VoxelChunkCoord2D {
  int32_t x = 0;
  int32_t z = 0;
};

struct TerrainColumnSample {
  float surface_height = 0.0f;
  float valley_factor = 0.0f;
};

constexpr uint64_t k_voxov_flat_world_seed = 0x0DDF00D5EEDull;

class WorldGenerator {
public:
  explicit WorldGenerator(uint64_t world_seed = 0);

  uint64_t world_seed() const;
  uint64_t chunk_seed(VoxelChunkCoord2D coord) const;
  TerrainColumnSample sample_column(float world_x, float world_z) const;
  float sample_height(float world_x, float world_z) const;

private:
  uint64_t seed = 0;
};

void sculpt_locomotion_course(VoxelChunk &chunk, int32_t chunk_x = 0,
                              int32_t chunk_z = 0);
void generate_flat_world_locomotion_chunk(
    VoxelChunk &chunk, uint64_t world_seed = k_voxov_flat_world_seed,
    int32_t chunk_x = 0, int32_t chunk_z = 0);
