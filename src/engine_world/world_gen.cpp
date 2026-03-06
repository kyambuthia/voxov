#include "engine_world/world_gen.hpp"

#include "engine_world/voxel_chunk.hpp"

#include <cmath>

namespace {
uint64_t splitmix64(uint64_t x) {
  x += 0x9e3779b97f4a7c15ull;
  x = (x ^ (x >> 30u)) * 0xbf58476d1ce4e5b9ull;
  x = (x ^ (x >> 27u)) * 0x94d049bb133111ebull;
  return x ^ (x >> 31u);
}
} // namespace

WorldGenerator::WorldGenerator(uint64_t world_seed) : seed(world_seed) {}

uint64_t WorldGenerator::world_seed() const { return seed; }

uint64_t WorldGenerator::chunk_seed(VoxelChunkCoord2D coord) const {
  uint64_t mixed = seed;
  mixed ^= static_cast<uint64_t>(static_cast<uint32_t>(coord.x)) *
           0x9e3779b97f4a7c15ull;
  mixed ^= static_cast<uint64_t>(static_cast<uint32_t>(coord.z)) *
           0xc2b2ae3d27d4eb4full;
  return splitmix64(mixed);
}

float WorldGenerator::sample_height(float world_x, float world_z) const {
  const float seed_phase_x = static_cast<float>((seed & 0xffffu)) * 0.00013f;
  const float seed_phase_z =
      static_cast<float>(((seed >> 16u) & 0xffffu)) * 0.00017f;
  const float low = std::sin(world_x * 0.11f + seed_phase_x) * 2.2f;
  const float medium = std::cos(world_z * 0.09f + seed_phase_z) * 1.8f;
  const float detail =
      std::sin((world_x + world_z) * 0.21f + seed_phase_x * 3.1f) * 0.8f;
  return 6.0f + low + medium + detail;
}

void sculpt_locomotion_course(VoxelChunk &chunk, int32_t chunk_x,
                              int32_t chunk_z) {
  if (chunk_x != 0 || chunk_z != 0) {
    return;
  }
  constexpr int base_y = 6;

  for (int z = 20; z <= 44; ++z) {
    for (int x = 20; x <= 44; ++x) {
      for (int y = 0; y < VoxelChunk::CHUNK_Y; ++y) {
        chunk.set_solid(x, y, z, y <= base_y);
      }
    }
  }

  for (int z = 24; z <= 30; ++z) {
    for (int x = 21; x <= 27; ++x) {
      const int terrace = (z - 24) / 2;
      for (int y = 0; y < VoxelChunk::CHUNK_Y; ++y) {
        chunk.set_solid(x, y, z, y <= base_y + terrace);
      }
    }
  }

  for (int step = 0; step < 4; ++step) {
    const int top_y = base_y + step;
    const int x0 = 34 + step * 2;
    const int x1 = x0 + 1;
    for (int z = 24; z <= 29; ++z) {
      for (int x = x0; x <= x1; ++x) {
        for (int y = 0; y < VoxelChunk::CHUNK_Y; ++y) {
          chunk.set_solid(x, y, z, y <= top_y);
        }
      }
    }
  }

  for (int z = 34; z <= 40; ++z) {
    for (int x = 26; x <= 32; ++x) {
      for (int y = base_y + 1; y < VoxelChunk::CHUNK_Y; ++y) {
        chunk.set_solid(x, y, z, false);
      }
    }
  }
}

void generate_flat_world_locomotion_chunk(VoxelChunk &chunk,
                                          uint64_t world_seed, int32_t chunk_x,
                                          int32_t chunk_z) {
  chunk.generate_heightmap_terrain_seeded(world_seed, chunk_x, chunk_z);
  sculpt_locomotion_course(chunk, chunk_x, chunk_z);
}
