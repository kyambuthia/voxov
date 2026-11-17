#include "engine_world/chunk_occlusion.hpp"

#include <queue>
#include <vector>

namespace {
struct Cell {
  int x = 0;
  int y = 0;
  int z = 0;
};

constexpr int kDirs[6][3] = {
    {-1, 0, 0}, {1, 0, 0}, {0, -1, 0},
    {0, 1, 0},  {0, 0, -1}, {0, 0, 1},
};
} // namespace

ChunkFaceVisibility compute_chunk_face_visibility(const VoxelChunk &chunk) {
  ChunkFaceVisibility vis{};
  if (chunk.is_empty()) {
    for (int f = 0; f < 6; ++f) {
      vis.reach[f] = 0x3Fu;
    }
    return vis;
  }

  const int sx = chunk.size_x();
  const int sy = chunk.size_y();
  const int sz = chunk.size_z();
  const size_t total =
      static_cast<size_t>(sx) * static_cast<size_t>(sy) * static_cast<size_t>(sz);
  std::vector<uint8_t> visited(total, 0);
  auto idx = [&](int x, int y, int z) -> size_t {
    return (static_cast<size_t>(z) * static_cast<size_t>(sy) +
            static_cast<size_t>(y)) *
               static_cast<size_t>(sx) +
           static_cast<size_t>(x);
  };

  std::queue<Cell> q;
  for (int z = 0; z < sz; ++z) {
    for (int y = 0; y < sy; ++y) {
      for (int x = 0; x < sx; ++x) {
        if (x != 0 && x != sx - 1 && y != 0 && y != sy - 1 && z != 0 &&
            z != sz - 1) {
          continue;
        }
        if (chunk.solid(x, y, z) || visited[idx(x, y, z)]) {
          continue;
        }

        uint8_t component_faces = 0;
        visited[idx(x, y, z)] = 1;
        q.push({x, y, z});
        while (!q.empty()) {
          const Cell cur = q.front();
          q.pop();
          if (cur.x == 0) component_faces |= 1u << 0;
          if (cur.x == sx - 1) component_faces |= 1u << 1;
          if (cur.y == 0) component_faces |= 1u << 2;
          if (cur.y == sy - 1) component_faces |= 1u << 3;
          if (cur.z == 0) component_faces |= 1u << 4;
          if (cur.z == sz - 1) component_faces |= 1u << 5;

          for (const auto &d : kDirs) {
            const int nx = cur.x + d[0];
            const int ny = cur.y + d[1];
            const int nz = cur.z + d[2];
            if (nx < 0 || nx >= sx || ny < 0 || ny >= sy || nz < 0 ||
                nz >= sz) {
              continue;
            }
            const size_t nidx = idx(nx, ny, nz);
            if (!visited[nidx] && !chunk.solid(nx, ny, nz)) {
              visited[nidx] = 1;
              q.push({nx, ny, nz});
            }
          }
        }

        for (int f = 0; f < 6; ++f) {
          if (component_faces & (1u << f)) {
            vis.reach[f] |= component_faces;
          }
        }
      }
    }
  }
  return vis;
}
