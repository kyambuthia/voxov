#include "engine_world/world_gen.hpp"

#include "engine_world/voxel_chunk.hpp"

#include <algorithm>
#include <cmath>

namespace {
uint64_t splitmix64(uint64_t x) {
  x += 0x9e3779b97f4a7c15ull;
  x = (x ^ (x >> 30u)) * 0xbf58476d1ce4e5b9ull;
  x = (x ^ (x >> 27u)) * 0x94d049bb133111ebull;
  return x ^ (x >> 31u);
}

float smoothstep01(float value) {
  const float t = std::clamp(value, 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

float smoothstep_range(float edge0, float edge1, float value) {
  return smoothstep01((value - edge0) / std::max(0.0001f, edge1 - edge0));
}

float sample_lattice(int32_t x, int32_t z, uint64_t seed, uint64_t salt) {
  uint64_t mixed = seed ^ salt;
  mixed ^= static_cast<uint64_t>(static_cast<uint32_t>(x)) *
           0x9e3779b97f4a7c15ull;
  mixed ^= static_cast<uint64_t>(static_cast<uint32_t>(z)) *
           0xc2b2ae3d27d4eb4full;
  const uint64_t hashed = splitmix64(mixed);
  const float unit =
      static_cast<float>((hashed >> 40u) & 0xffffffu) / 16777215.0f;
  return unit * 2.0f - 1.0f;
}

float value_noise(float world_x, float world_z, float wavelength, uint64_t seed,
                  uint64_t salt) {
  const float sample_x = world_x / wavelength;
  const float sample_z = world_z / wavelength;
  const int32_t x0 = static_cast<int32_t>(std::floor(sample_x));
  const int32_t z0 = static_cast<int32_t>(std::floor(sample_z));
  const float tx = smoothstep01(sample_x - static_cast<float>(x0));
  const float tz = smoothstep01(sample_z - static_cast<float>(z0));
  const float a = std::lerp(sample_lattice(x0, z0, seed, salt),
                            sample_lattice(x0 + 1, z0, seed, salt), tx);
  const float b = std::lerp(sample_lattice(x0, z0 + 1, seed, salt),
                            sample_lattice(x0 + 1, z0 + 1, seed, salt), tx);
  return std::lerp(a, b, tz);
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

TerrainColumnSample WorldGenerator::sample_column(float world_x,
                                                   float world_z) const {
  const float continental =
      value_noise(world_x, world_z, 256.0f, seed, 0x2f6e2b1d54a32f71ull);
  const float broad_hills =
      value_noise(world_x, world_z, 112.0f, seed, 0xb43ac574127b3b55ull);
  const float rolling_hills =
      value_noise(world_x, world_z, 46.0f, seed, 0x9642a7c13d42e0bdull);
  const float surface_detail =
      value_noise(world_x, world_z, 18.0f, seed, 0x6f54789be80a9d2bull);

  const float ridge_source =
      1.0f - std::fabs(value_noise(world_x, world_z, 92.0f, seed,
                                   0xe290a3ac986dc3d7ull));
  const float ridge_factor = smoothstep_range(0.38f, 0.86f, ridge_source);

  const float valley_source =
      1.0f - std::fabs(value_noise(world_x, world_z, 176.0f, seed,
                                   0x47dfd298cba61d31ull));
  const float valley_factor = smoothstep_range(0.56f, 0.94f, valley_source);

  float surface_height = 11.0f;
  surface_height += continental * 4.0f;
  surface_height += broad_hills * 3.4f;
  surface_height += rolling_hills * 2.1f;
  surface_height += ridge_factor * ridge_factor * 4.8f;
  surface_height -= valley_factor * (3.2f + ridge_factor * 1.7f);
  surface_height += surface_detail * 0.7f;

  TerrainColumnSample sample{};
  sample.surface_height =
      std::clamp(surface_height, 2.0f,
                 static_cast<float>(VoxelChunk::CHUNK_Y - 3));
  sample.valley_factor = valley_factor;
  return sample;
}

float WorldGenerator::sample_height(float world_x, float world_z) const {
  return sample_column(world_x, world_z).surface_height;
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

void sculpt_chunk_landmark(VoxelChunk &chunk, uint64_t world_seed,
                           int32_t chunk_x, int32_t chunk_z) {
  if (chunk_x == 0 && chunk_z == 0) {
    return;
  }
  if ((std::abs(chunk_x) + std::abs(chunk_z)) < 2) {
    return;
  }

  const WorldGenerator generator(world_seed);
  const uint64_t seed = generator.chunk_seed({chunk_x, chunk_z});
  if ((seed & 0x1u) == 0u) {
    return;
  }

  const int center_x = 18 + static_cast<int>((seed >> 8u) % 28u);
  const int center_z = 18 + static_cast<int>((seed >> 16u) % 28u);
  const int pedestal_top = 7 + static_cast<int>((seed >> 24u) % 3u);
  const int tower_top = std::min(VoxelChunk::CHUNK_Y - 2, pedestal_top + 6);

  for (int z = center_z - 3; z <= center_z + 3; ++z) {
    for (int x = center_x - 3; x <= center_x + 3; ++x) {
      for (int y = pedestal_top + 1; y < VoxelChunk::CHUNK_Y; ++y) {
        chunk.set_solid(x, y, z, false);
      }
      for (int y = 0; y <= pedestal_top; ++y) {
        chunk.set_solid(x, y, z, true);
      }
    }
  }

  for (int z = center_z - 1; z <= center_z + 1; ++z) {
    for (int x = center_x - 1; x <= center_x + 1; ++x) {
      for (int y = pedestal_top + 1; y <= tower_top; ++y) {
        chunk.set_solid(x, y, z, true);
      }
    }
  }

  for (int z = center_z - 2; z <= center_z + 2; ++z) {
    for (int x = center_x - 2; x <= center_x + 2; ++x) {
      chunk.set_solid(x, tower_top + 1, z, true);
    }
  }
}

void generate_flat_world_locomotion_chunk(VoxelChunk &chunk,
                                          uint64_t world_seed, int32_t chunk_x,
                                          int32_t chunk_z) {
  chunk.generate_heightmap_terrain_seeded(world_seed, chunk_x, chunk_z);
  sculpt_locomotion_course(chunk, chunk_x, chunk_z);
  sculpt_chunk_landmark(chunk, world_seed, chunk_x, chunk_z);
}
