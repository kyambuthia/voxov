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
  const float warp_x =
      value_noise(world_x, world_z, 128.0f, seed, 0x1a2b3c4d5e6f7081ull) * 24.0f;
  const float warp_z =
      value_noise(world_x, world_z, 128.0f, seed, 0x9182736455463728ull) * 24.0f;
  const float wx = world_x + warp_x;
  const float wz = world_z + warp_z;

  const float continental =
      value_noise(wx, wz, 220.0f, seed, 0x2f6e2b1d54a32f71ull);
  const float broad_hills =
      value_noise(wx, wz, 78.0f, seed, 0xb43ac574127b3b55ull);
  const float rolling_hills =
      value_noise(wx, wz, 34.0f, seed, 0x9642a7c13d42e0bdull);
  const float surface_detail =
      value_noise(world_x, world_z, 14.0f, seed, 0x6f54789be80a9d2bull);

  const float ridge_source =
      1.0f - std::fabs(value_noise(wx, wz, 72.0f, seed,
                                   0xe290a3ac986dc3d7ull));
  const float ridge_factor = smoothstep_range(0.30f, 0.84f, ridge_source);

  const float valley_source =
      1.0f - std::fabs(value_noise(wx, wz, 110.0f, seed,
                                   0x47dfd298cba61d31ull));
  const float valley_factor = smoothstep_range(0.45f, 0.92f, valley_source);

  float surface_height = 13.5f;
  surface_height += continental * 5.2f;
  surface_height += broad_hills * 5.6f;
  surface_height += rolling_hills * 3.0f;
  surface_height += ridge_factor * ridge_factor * 10.5f;
  surface_height -= valley_factor * (6.8f + ridge_factor * 2.5f);
  surface_height += surface_detail * 0.9f;

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



PlanetBiome classify_planet_biome(double temperature, double moisture,
                                  double elevation_norm) {
  if (elevation_norm > 0.72) {
    return PlanetBiome::Highlands;
  }
  if (temperature < 0.30) {
    return PlanetBiome::Tundra;
  }
  if (temperature > 0.65 && moisture < 0.35) {
    return PlanetBiome::Desert;
  }
  return PlanetBiome::Plains;
}

namespace {
uint64_t hash_3d_int(int64_t x, int64_t y, int64_t z, uint64_t seed) {
  uint64_t h = seed ^ (static_cast<uint64_t>(x) * 0x9e3779b97f4a7c15ull);
  h ^= static_cast<uint64_t>(y) * 0xbf58476d1ce4e5b9ull;
  h ^= static_cast<uint64_t>(z) * 0x94d049bb133111ebull;
  h ^= h >> 30;
  h *= 0xbf58476d1ce4e5b9ull;
  h ^= h >> 27;
  return h;
}

double value_noise_3d(double x, double y, double z, uint64_t seed) {
  const int64_t ix = static_cast<int64_t>(std::floor(x));
  const int64_t iy = static_cast<int64_t>(std::floor(y));
  const int64_t iz = static_cast<int64_t>(std::floor(z));
  const double fx = x - static_cast<double>(ix);
  const double fy = y - static_cast<double>(iy);
  const double fz = z - static_cast<double>(iz);
  const double ux = fx * fx * (3.0 - 2.0 * fx);
  const double uy = fy * fy * (3.0 - 2.0 * fy);
  const double uz = fz * fz * (3.0 - 2.0 * fz);
  auto corner = [&](int dx, int dy, int dz) -> double {
    const uint64_t h = hash_3d_int(ix + dx, iy + dy, iz + dz, seed);
    return static_cast<double>(h & 0xFFFFFFu) / static_cast<double>(0xFFFFFFu);
  };
  const double c000 = corner(0, 0, 0), c100 = corner(1, 0, 0);
  const double c010 = corner(0, 1, 0), c110 = corner(1, 1, 0);
  const double c001 = corner(0, 0, 1), c101 = corner(1, 0, 1);
  const double c011 = corner(0, 1, 1), c111 = corner(1, 1, 1);
  const double x00 = c000 + (c100 - c000) * ux;
  const double x10 = c010 + (c110 - c010) * ux;
  const double x01 = c001 + (c101 - c001) * ux;
  const double x11 = c011 + (c111 - c011) * ux;
  const double y0 = x00 + (x10 - x00) * uy;
  const double y1 = x01 + (x11 - x01) * uy;
  return y0 + (y1 - y0) * uz;
}
} // namespace

float sample_cave_density_3d(double x, double y, double z, uint64_t seed) {
  const double n1 = value_noise_3d(x * 0.08, y * 0.08, z * 0.08, seed);
  const double n2 = value_noise_3d(x * 0.17, y * 0.17, z * 0.17, seed ^ 0x55aa3311ull);
  return static_cast<float>((n1 * 0.7 + n2 * 0.3) * 2.0 - 1.0);
}
