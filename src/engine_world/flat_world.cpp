#include "engine_world/flat_world.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
int32_t floor_div_int(int32_t a, int32_t b) {
  int32_t q = a / b;
  int32_t r = a % b;
  if (r != 0 && ((r < 0) != (b < 0))) {
    --q;
  }
  return q;
}

int pos_mod_int(int32_t a, int32_t b) {
  int32_t r = a % b;
  if (r < 0) {
    r += b;
  }
  return r;
}
} // namespace

void FlatWorld::init(const FlatWorldConfig &config) {
  config_ = config;
  generator_ = WorldGenerator(config_.seed);
  chunks_.clear();
}

VoxelChunkCoord2D FlatWorld::world_to_chunk(int32_t wx, int32_t wz) {
  return VoxelChunkCoord2D{
      floor_div_int(wx, VoxelChunk::CHUNK_X),
      floor_div_int(wz, VoxelChunk::CHUNK_Z),
  };
}

VoxelChunkCoord2D FlatWorld::world_pos_to_chunk(const glm::dvec3 &world_pos,
                                                float voxel_scale) {
  const double inv_scale = voxel_scale > 0.0f ? 1.0 / voxel_scale : 1.0;
  const int32_t wx = static_cast<int32_t>(std::floor(world_pos.x * inv_scale));
  const int32_t wz = static_cast<int32_t>(std::floor(world_pos.z * inv_scale));
  return world_to_chunk(wx, wz);
}

void FlatWorld::world_to_local(int32_t wx, int32_t wz,
                               VoxelChunkCoord2D &out_chunk,
                               int &out_lx, int &out_lz) {
  out_chunk = world_to_chunk(wx, wz);
  out_lx = pos_mod_int(wx, VoxelChunk::CHUNK_X);
  out_lz = pos_mod_int(wz, VoxelChunk::CHUNK_Z);
}

VoxelChunk *FlatWorld::find_chunk(VoxelChunkCoord2D coord) {
  auto it = chunks_.find(coord);
  return it != chunks_.end() ? &it->second : nullptr;
}

const VoxelChunk *FlatWorld::find_chunk(VoxelChunkCoord2D coord) const {
  auto it = chunks_.find(coord);
  return it != chunks_.end() ? &it->second : nullptr;
}

VoxelChunk &FlatWorld::get_or_generate_chunk(VoxelChunkCoord2D coord) {
  auto it = chunks_.find(coord);
  if (it != chunks_.end()) {
    return it->second;
  }
  VoxelChunk chunk;
  chunk.generate_heightmap_terrain_seeded(config_.seed, coord.x, coord.z);
  auto [inserted_it, _] = chunks_.emplace(coord, std::move(chunk));
  return inserted_it->second;
}

bool FlatWorld::solid_at(int32_t wx, int32_t wy, int32_t wz) const {
  if (wy < 0 || wy >= VoxelChunk::CHUNK_Y) {
    return false;
  }
  VoxelChunkCoord2D coord{};
  int lx = 0;
  int lz = 0;
  world_to_local(wx, wz, coord, lx, lz);
  const VoxelChunk *chunk = find_chunk(coord);
  if (!chunk) {
    return false;
  }
  return chunk->solid(lx, wy, lz);
}

VoxelMaterial FlatWorld::material_at(int32_t wx, int32_t wy, int32_t wz) const {
  if (wy < 0 || wy >= VoxelChunk::CHUNK_Y) {
    return VoxelMaterial::Air;
  }
  VoxelChunkCoord2D coord{};
  int lx = 0;
  int lz = 0;
  world_to_local(wx, wz, coord, lx, lz);
  const VoxelChunk *chunk = find_chunk(coord);
  if (!chunk) {
    return VoxelMaterial::Air;
  }
  return chunk->material(lx, wy, lz);
}

bool FlatWorld::set_block(int32_t wx, int32_t wy, int32_t wz,
                          VoxelMaterial material) {
  if (wy < 0 || wy >= VoxelChunk::CHUNK_Y) {
    return false;
  }
  VoxelChunkCoord2D coord{};
  int lx = 0;
  int lz = 0;
  world_to_local(wx, wz, coord, lx, lz);
  VoxelChunk &chunk = get_or_generate_chunk(coord);
  if (material == VoxelMaterial::Air) {
    chunk.set_material(lx, wy, lz, VoxelMaterial::Air, 0);
  } else {
    chunk.set_material(lx, wy, lz, material, VoxelChunk::kMaxBlockHeight);
  }
  return true;
}

float FlatWorld::surface_height_at(float wx, float wz) const {
  return generator_.sample_height(wx, wz);
}

std::vector<VoxelChunkCoord2D>
FlatWorld::stream_around(const glm::dvec3 &world_pos, uint32_t gen_budget,
                         std::vector<VoxelChunkCoord2D> &out_evicted) {
  out_evicted.clear();
  const VoxelChunkCoord2D center =
      world_pos_to_chunk(world_pos, config_.voxel_scale);

  // Evict chunks outside evict_radius_chunks.
  for (auto it = chunks_.begin(); it != chunks_.end();) {
    const int32_t dx = std::abs(it->first.x - center.x);
    const int32_t dz = std::abs(it->first.z - center.z);
    if (std::max(dx, dz) > config_.evict_radius_chunks) {
      out_evicted.push_back(it->first);
      it = chunks_.erase(it);
    } else {
      ++it;
    }
  }

  // Generate missing chunks closest-first up to gen_budget.
  std::vector<VoxelChunkCoord2D> missing;
  const int32_t r = config_.stream_radius_chunks;
  missing.reserve(static_cast<size_t>((2 * r + 1) * (2 * r + 1)));
  for (int32_t dz = -r; dz <= r; ++dz) {
    for (int32_t dx = -r; dx <= r; ++dx) {
      const VoxelChunkCoord2D c{center.x + dx, center.z + dz};
      if (chunks_.find(c) == chunks_.end()) {
        missing.push_back(c);
      }
    }
  }

  std::sort(missing.begin(), missing.end(),
            [&](const VoxelChunkCoord2D &a, const VoxelChunkCoord2D &b) {
              const int da = (a.x - center.x) * (a.x - center.x) +
                             (a.z - center.z) * (a.z - center.z);
              const int db = (b.x - center.x) * (b.x - center.x) +
                             (b.z - center.z) * (b.z - center.z);
              return da < db;
            });

  std::vector<VoxelChunkCoord2D> generated;
  const uint32_t limit =
      std::min<uint32_t>(gen_budget, static_cast<uint32_t>(missing.size()));
  generated.reserve(limit);
  for (uint32_t i = 0; i < limit; ++i) {
    get_or_generate_chunk(missing[i]);
    generated.push_back(missing[i]);
  }
  return generated;
}

std::vector<VoxelCollisionChunk>
FlatWorld::collect_collision_chunks(const glm::dvec3 &world_pos,
                                    int32_t radius_chunks) const {
  const VoxelChunkCoord2D center =
      world_pos_to_chunk(world_pos, config_.voxel_scale);
  std::vector<VoxelCollisionChunk> result;
  result.reserve(
      static_cast<size_t>((2 * radius_chunks + 1) * (2 * radius_chunks + 1)));
  for (int32_t dz = -radius_chunks; dz <= radius_chunks; ++dz) {
    for (int32_t dx = -radius_chunks; dx <= radius_chunks; ++dx) {
      const VoxelChunkCoord2D c{center.x + dx, center.z + dz};
      if (const VoxelChunk *chunk = find_chunk(c)) {
        result.push_back(VoxelCollisionChunk{
            chunk,
            c.x * VoxelChunk::CHUNK_X,
            c.z * VoxelChunk::CHUNK_Z,
        });
      }
    }
  }
  return result;
}

bool FlatWorld::raycast(const glm::dvec3 &origin, const glm::dvec3 &direction,
                        float max_distance, FlatBlockAddress &out_hit,
                        glm::vec3 &out_normal) const {
  const double len = glm::length(direction);
  if (len < 1.0e-8 || max_distance <= 0.0f) {
    return false;
  }
  const glm::dvec3 dir = direction / len;
  const double inv_scale =
      config_.voxel_scale > 0.0f ? 1.0 / config_.voxel_scale : 1.0;

  glm::dvec3 pos = origin * inv_scale;
  int32_t vx = static_cast<int32_t>(std::floor(pos.x));
  int32_t vy = static_cast<int32_t>(std::floor(pos.y));
  int32_t vz = static_cast<int32_t>(std::floor(pos.z));

  const int step_x = dir.x > 0.0 ? 1 : (dir.x < 0.0 ? -1 : 0);
  const int step_y = dir.y > 0.0 ? 1 : (dir.y < 0.0 ? -1 : 0);
  const int step_z = dir.z > 0.0 ? 1 : (dir.z < 0.0 ? -1 : 0);

  const double t_delta_x =
      step_x != 0 ? std::abs(1.0 / dir.x) : std::numeric_limits<double>::max();
  const double t_delta_y =
      step_y != 0 ? std::abs(1.0 / dir.y) : std::numeric_limits<double>::max();
  const double t_delta_z =
      step_z != 0 ? std::abs(1.0 / dir.z) : std::numeric_limits<double>::max();

  double t_max_x = step_x > 0
                       ? (static_cast<double>(vx + 1) - pos.x) * t_delta_x
                       : (pos.x - static_cast<double>(vx)) * t_delta_x;
  double t_max_y = step_y > 0
                       ? (static_cast<double>(vy + 1) - pos.y) * t_delta_y
                       : (pos.y - static_cast<double>(vy)) * t_delta_y;
  double t_max_z = step_z > 0
                       ? (static_cast<double>(vz + 1) - pos.z) * t_delta_z
                       : (pos.z - static_cast<double>(vz)) * t_delta_z;
  if (step_x == 0) t_max_x = std::numeric_limits<double>::max();
  if (step_y == 0) t_max_y = std::numeric_limits<double>::max();
  if (step_z == 0) t_max_z = std::numeric_limits<double>::max();

  const double max_t = static_cast<double>(max_distance) * inv_scale;
  double t = 0.0;
  glm::vec3 hit_normal(0.0f);

  while (t <= max_t) {
    if (solid_at(vx, vy, vz)) {
      out_hit = FlatBlockAddress{vx, vy, vz};
      out_normal = hit_normal;
      return true;
    }

    if (t_max_x < t_max_y) {
      if (t_max_x < t_max_z) {
        t = t_max_x;
        t_max_x += t_delta_x;
        vx += step_x;
        hit_normal = glm::vec3(static_cast<float>(-step_x), 0.0f, 0.0f);
      } else {
        t = t_max_z;
        t_max_z += t_delta_z;
        vz += step_z;
        hit_normal = glm::vec3(0.0f, 0.0f, static_cast<float>(-step_z));
      }
    } else {
      if (t_max_y < t_max_z) {
        t = t_max_y;
        t_max_y += t_delta_y;
        vy += step_y;
        hit_normal = glm::vec3(0.0f, static_cast<float>(-step_y), 0.0f);
      } else {
        t = t_max_z;
        t_max_z += t_delta_z;
        vz += step_z;
        hit_normal = glm::vec3(0.0f, 0.0f, static_cast<float>(-step_z));
      }
    }
  }
  return false;
}
