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
  constexpr float kWaterLevel = 14.0f;

  // Domain warp for natural meandering rivers and organic mountain ridges.
  const float warp_x =
      value_noise(world_x, world_z, 140.0f, seed, 0x1a2b3c4d5e6f7081ull) *
      24.0f;
  const float warp_z =
      value_noise(world_x, world_z, 140.0f, seed, 0x81706f5e4d3c2b1aull) *
      24.0f;
  const float wx = world_x + warp_x;
  const float wz = world_z + warp_z;

  const float continental =
      value_noise(world_x, world_z, 340.0f, seed, 0x2f6e2b1d54a32f71ull);
  const float broad_hills =
      value_noise(world_x, world_z, 112.0f, seed, 0xb43ac574127b3b55ull);
  const float rolling_hills =
      value_noise(world_x, world_z, 46.0f, seed, 0x9642a7c13d42e0bdull);
  const float surface_detail =
      value_noise(world_x, world_z, 18.0f, seed, 0x6f54789be80a9d2bull);

  const float valley_source =
      1.0f - std::fabs(value_noise(wx, wz, 210.0f, seed,
                                   0x47dfd298cba61d31ull));
  const float valley_factor = smoothstep_range(0.55f, 0.94f, valley_source);

  const float mountain_region = smoothstep_range(
      -0.10f, 0.55f,
      value_noise(world_x, world_z, 180.0f, seed, 0x51a7c9e23f8014d3ull));
  const float ridge_raw =
      1.0f - std::fabs(value_noise(wx, wz, 96.0f, seed,
                                   0xe290a3ac986dc3d7ull));
  const float ridge_sharp = smoothstep_range(0.28f, 0.85f, ridge_raw);
  const float mountain_factor =
      std::clamp(mountain_region * (0.35f + 0.65f * ridge_sharp) *
                     (1.0f - 0.60f * valley_factor),
                 0.0f, 1.0f);

  float surface_height = 17.5f;
  surface_height += continental * 4.5f;
  surface_height += broad_hills * 3.5f;
  surface_height += rolling_hills * 2.0f;
  surface_height += std::pow(mountain_factor, 1.3f) * 32.0f;
  surface_height -= valley_factor * 5.0f;
  surface_height += surface_detail * 0.8f;

  // Meandering river channels carved through lowlands and valleys.
  const float river_source =
      1.0f - std::fabs(value_noise(wx, wz, 240.0f, seed,
                                   0x7c3e91a4b2d805f1ull));
  const float river_channel = smoothstep_range(0.84f, 0.96f, river_source);
  const float river_mountain_mask =
      1.0f - smoothstep_range(0.38f, 0.78f, mountain_factor);
  const float river_factor =
      std::clamp(river_channel * river_mountain_mask, 0.0f, 1.0f);

  if (river_factor > 0.0f) {
    const float river_bed_height =
        kWaterLevel - 3.2f + surface_detail * 0.4f;
    surface_height =
        std::lerp(surface_height, std::min(surface_height, river_bed_height),
                  river_factor);
  }

  TerrainColumnSample sample{};
  sample.surface_height =
      std::clamp(surface_height, 2.0f,
                 static_cast<float>(VoxelChunk::CHUNK_Y - 3));
  sample.valley_factor = valley_factor;
  sample.mountain_factor = mountain_factor;
  sample.river_factor = river_factor;
  sample.water_level = kWaterLevel;
  return sample;
}

float WorldGenerator::sample_height(float world_x, float world_z) const {
  return sample_column(world_x, world_z).surface_height;
}


