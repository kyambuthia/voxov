#include "engine_world/voxel_lighting.hpp"

#include <algorithm>
#include <queue>

namespace {
struct LightNode {
  int x = 0;
  int y = 0;
  int z = 0;
};

struct LightRemovalNode {
  int x = 0;
  int y = 0;
  int z = 0;
  uint8_t old_level = 0;
};

constexpr int kDirs[6][3] = {
    {-1, 0, 0}, {1, 0, 0}, {0, -1, 0},
    {0, 1, 0},  {0, 0, -1}, {0, 0, 1},
};
} // namespace

VoxelLightMap::VoxelLightMap(int size_x, int size_y, int size_z) {
  resize(size_x, size_y, size_z);
}

void VoxelLightMap::resize(int size_x, int size_y, int size_z) {
  size_x_ = std::max(0, size_x);
  size_y_ = std::max(0, size_y);
  size_z_ = std::max(0, size_z);
  packed_.assign(
      static_cast<size_t>(size_x_) * static_cast<size_t>(size_y_) *
          static_cast<size_t>(size_z_),
      0);
}

void VoxelLightMap::clear() { std::fill(packed_.begin(), packed_.end(), 0); }

bool VoxelLightMap::in_bounds(int x, int y, int z) const {
  return x >= 0 && x < size_x_ && y >= 0 && y < size_y_ && z >= 0 &&
         z < size_z_;
}

size_t VoxelLightMap::index(int x, int y, int z) const {
  return (static_cast<size_t>(z) * static_cast<size_t>(size_y_) +
          static_cast<size_t>(y)) *
             static_cast<size_t>(size_x_) +
         static_cast<size_t>(x);
}

uint8_t VoxelLightMap::sky_light(int x, int y, int z) const {
  if (!in_bounds(x, y, z)) {
    return 15;
  }
  return static_cast<uint8_t>((packed_[index(x, y, z)] >> 4) & 0x0Fu);
}

uint8_t VoxelLightMap::block_light(int x, int y, int z) const {
  if (!in_bounds(x, y, z)) {
    return 0;
  }
  return static_cast<uint8_t>(packed_[index(x, y, z)] & 0x0Fu);
}

void VoxelLightMap::set_sky_light(int x, int y, int z, uint8_t level) {
  if (!in_bounds(x, y, z)) {
    return;
  }
  const size_t idx = index(x, y, z);
  const uint8_t clamped = std::min<uint8_t>(level, 15);
  packed_[idx] = static_cast<uint8_t>((packed_[idx] & 0x0Fu) | (clamped << 4));
}

void VoxelLightMap::set_block_light(int x, int y, int z, uint8_t level) {
  if (!in_bounds(x, y, z)) {
    return;
  }
  const size_t idx = index(x, y, z);
  const uint8_t clamped = std::min<uint8_t>(level, 15);
  packed_[idx] = static_cast<uint8_t>((packed_[idx] & 0xF0u) | clamped);
}

void VoxelLightMap::compute_sunlight(const VoxelChunk &chunk) {
  if (chunk.size_x() != size_x_ || chunk.size_y() != size_y_ ||
      chunk.size_z() != size_z_) {
    resize(chunk.size_x(), chunk.size_y(), chunk.size_z());
  }
  for (uint8_t &v : packed_) {
    v &= 0x0Fu;
  }

  std::queue<LightNode> bfs;
  for (int z = 0; z < size_z_; ++z) {
    for (int x = 0; x < size_x_; ++x) {
      for (int y = size_y_ - 1; y >= 0; --y) {
        if (chunk.solid(x, y, z)) {
          break;
        }
        set_sky_light(x, y, z, 15);
        bfs.push({x, y, z});
      }
    }
  }

  while (!bfs.empty()) {
    const LightNode cur = bfs.front();
    bfs.pop();
    const uint8_t cur_sky = sky_light(cur.x, cur.y, cur.z);
    if (cur_sky <= 1) {
      continue;
    }
    const uint8_t next_sky = static_cast<uint8_t>(cur_sky - 1);
    for (const auto &d : kDirs) {
      const int nx = cur.x + d[0];
      const int ny = cur.y + d[1];
      const int nz = cur.z + d[2];
      if (!in_bounds(nx, ny, nz) || chunk.solid(nx, ny, nz)) {
        continue;
      }
      if (sky_light(nx, ny, nz) < next_sky) {
        set_sky_light(nx, ny, nz, next_sky);
        bfs.push({nx, ny, nz});
      }
    }
  }
}

void VoxelLightMap::add_block_light(const VoxelChunk &chunk, int x, int y,
                                    int z, uint8_t level) {
  if (!in_bounds(x, y, z)) {
    return;
  }
  const uint8_t clamped = std::min<uint8_t>(level, 15);
  if (block_light(x, y, z) >= clamped) {
    return;
  }
  set_block_light(x, y, z, clamped);
  std::queue<LightNode> bfs;
  bfs.push({x, y, z});

  while (!bfs.empty()) {
    const LightNode cur = bfs.front();
    bfs.pop();
    const uint8_t cur_l = block_light(cur.x, cur.y, cur.z);
    if (cur_l <= 1) {
      continue;
    }
    const uint8_t next_l = static_cast<uint8_t>(cur_l - 1);
    for (const auto &d : kDirs) {
      const int nx = cur.x + d[0];
      const int ny = cur.y + d[1];
      const int nz = cur.z + d[2];
      if (!in_bounds(nx, ny, nz) || chunk.solid(nx, ny, nz)) {
        continue;
      }
      if (block_light(nx, ny, nz) < next_l) {
        set_block_light(nx, ny, nz, next_l);
        bfs.push({nx, ny, nz});
      }
    }
  }
}

void VoxelLightMap::remove_block_light(const VoxelChunk &chunk, int x, int y,
                                       int z) {
  if (!in_bounds(x, y, z)) {
    return;
  }
  const uint8_t start_l = block_light(x, y, z);
  if (start_l == 0) {
    return;
  }

  std::queue<LightRemovalNode> remove_q;
  std::queue<LightNode> refill_q;
  set_block_light(x, y, z, 0);
  remove_q.push({x, y, z, start_l});

  while (!remove_q.empty()) {
    const LightRemovalNode cur = remove_q.front();
    remove_q.pop();
    for (const auto &d : kDirs) {
      const int nx = cur.x + d[0];
      const int ny = cur.y + d[1];
      const int nz = cur.z + d[2];
      if (!in_bounds(nx, ny, nz)) {
        continue;
      }
      const uint8_t nl = block_light(nx, ny, nz);
      if (nl != 0 && nl < cur.old_level) {
        set_block_light(nx, ny, nz, 0);
        remove_q.push({nx, ny, nz, nl});
      } else if (nl >= cur.old_level) {
        refill_q.push({nx, ny, nz});
      }
    }
  }

  while (!refill_q.empty()) {
    const LightNode cur = refill_q.front();
    refill_q.pop();
    const uint8_t cur_l = block_light(cur.x, cur.y, cur.z);
    if (cur_l <= 1) {
      continue;
    }
    const uint8_t next_l = static_cast<uint8_t>(cur_l - 1);
    for (const auto &d : kDirs) {
      const int nx = cur.x + d[0];
      const int ny = cur.y + d[1];
      const int nz = cur.z + d[2];
      if (!in_bounds(nx, ny, nz) || chunk.solid(nx, ny, nz)) {
        continue;
      }
      if (block_light(nx, ny, nz) < next_l) {
        set_block_light(nx, ny, nz, next_l);
        refill_q.push({nx, ny, nz});
      }
    }
  }
}

float VoxelLightMap::brightness(int x, int y, int z) const {
  const uint8_t level = std::max(sky_light(x, y, z), block_light(x, y, z));
  return 0.12f + 0.88f * (static_cast<float>(level) / 15.0f);
}
