#pragma once

#include "engine_world/physics/voxel_collision.hpp"
#include "engine_world/voxel_chunk.hpp"
#include "engine_world/world_gen.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <unordered_map>
#include <vector>

struct VoxelChunkCoord2DHash {
  size_t operator()(const VoxelChunkCoord2D &c) const noexcept {
    const uint64_t kx = static_cast<uint32_t>(c.x);
    const uint64_t kz = static_cast<uint32_t>(c.z);
    uint64_t h = (kx << 32u) | kz;
    h ^= h >> 33u;
    h *= 0xff51afd7ed558ccdull;
    h ^= h >> 33u;
    return static_cast<size_t>(h);
  }
};

inline bool operator==(const VoxelChunkCoord2D &a, const VoxelChunkCoord2D &b) {
  return a.x == b.x && a.z == b.z;
}

struct FlatBlockAddress {
  int32_t wx = 0;
  int32_t wy = 0;
  int32_t wz = 0;

  friend bool operator==(const FlatBlockAddress &,
                         const FlatBlockAddress &) = default;
};

struct FlatBlockAddressHash {
  size_t operator()(const FlatBlockAddress &a) const noexcept {
    uint64_t h = static_cast<uint32_t>(a.wx) * 0x9e3779b97f4a7c15ull;
    h ^= static_cast<uint32_t>(a.wy) * 0xbf58476d1ce4e5b9ull;
    h ^= static_cast<uint32_t>(a.wz) * 0x94d049bb133111ebull;
    h ^= h >> 31u;
    return static_cast<size_t>(h);
  }
};

struct FlatWorldConfig {
  uint64_t seed = k_voxov_flat_world_seed;
  int32_t stream_radius_chunks = 3;
  int32_t evict_radius_chunks = 5;
  float voxel_scale = 1.0f;
};

class FlatWorld {
public:
  void init(const FlatWorldConfig &config = FlatWorldConfig{});
  const FlatWorldConfig &config() const { return config_; }
  const WorldGenerator &generator() const { return generator_; }

  static VoxelChunkCoord2D world_to_chunk(int32_t wx, int32_t wz);
  static VoxelChunkCoord2D world_pos_to_chunk(const glm::dvec3 &world_pos,
                                              float voxel_scale = 1.0f);
  static void world_to_local(int32_t wx, int32_t wz,
                             VoxelChunkCoord2D &out_chunk,
                             int &out_lx, int &out_lz);

  VoxelChunk *find_chunk(VoxelChunkCoord2D coord);
  const VoxelChunk *find_chunk(VoxelChunkCoord2D coord) const;
  VoxelChunk &get_or_generate_chunk(VoxelChunkCoord2D coord);

  bool solid_at(int32_t wx, int32_t wy, int32_t wz) const;
  VoxelMaterial material_at(int32_t wx, int32_t wy, int32_t wz) const;
  bool set_block(int32_t wx, int32_t wy, int32_t wz, VoxelMaterial material);

  float surface_height_at(float wx, float wz) const;
  size_t chunk_count() const { return chunks_.size(); }
  const std::unordered_map<VoxelChunkCoord2D, VoxelChunk, VoxelChunkCoord2DHash> &
  chunks() const {
    return chunks_;
  }

  // Streams chunks around `world_pos` up to `gen_budget` per call and evicts
  // chunks beyond `evict_radius_chunks`. Returns newly generated chunk coords.
  std::vector<VoxelChunkCoord2D>
  stream_around(const glm::dvec3 &world_pos, uint32_t gen_budget,
                std::vector<VoxelChunkCoord2D> &out_evicted);

  std::vector<VoxelCollisionChunk>
  collect_collision_chunks(const glm::dvec3 &world_pos,
                           int32_t radius_chunks = 1) const;

  bool raycast(const glm::dvec3 &origin, const glm::dvec3 &direction,
               float max_distance, FlatBlockAddress &out_hit,
               glm::vec3 &out_normal) const;

private:
  FlatWorldConfig config_{};
  WorldGenerator generator_{k_voxov_flat_world_seed};
  std::unordered_map<VoxelChunkCoord2D, VoxelChunk, VoxelChunkCoord2DHash>
      chunks_;
};
