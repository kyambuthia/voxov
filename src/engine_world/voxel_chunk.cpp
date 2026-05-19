#include "engine_world/voxel_chunk.hpp"
#include "engine_world/world_gen.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {
void append_greedy_quad(RenderMesh &mesh, const glm::vec3 &origin,
                        const glm::ivec3 &base, const glm::ivec3 &q,
                        const glm::ivec3 &du, const glm::ivec3 &dv,
                        bool positive_face, const glm::vec3 &color,
                        float voxel_scale) {
  const uint32_t start = static_cast<uint32_t>(mesh.vertices.size());
  glm::vec3 p0(0.0f);
  glm::vec3 p1(0.0f);
  glm::vec3 p2(0.0f);
  glm::vec3 p3(0.0f);

  if (positive_face) {
    p0 = origin + glm::vec3(base) * voxel_scale;
    p1 = origin + glm::vec3(base + du) * voxel_scale;
    p2 = origin + glm::vec3(base + du + dv) * voxel_scale;
    p3 = origin + glm::vec3(base + dv) * voxel_scale;
  } else {
    p0 = origin + glm::vec3(base) * voxel_scale;
    p1 = origin + glm::vec3(base + dv) * voxel_scale;
    p2 = origin + glm::vec3(base + du + dv) * voxel_scale;
    p3 = origin + glm::vec3(base + du) * voxel_scale;
  }

  const glm::vec3 normal =
      positive_face ? glm::normalize(glm::vec3(q)) : -glm::normalize(glm::vec3(q));
  mesh.vertices.push_back({p0, color, normal});
  mesh.vertices.push_back({p1, color, normal});
  mesh.vertices.push_back({p2, color, normal});
  mesh.vertices.push_back({p3, color, normal});
  mesh.indices.insert(mesh.indices.end(), {start, start + 1, start + 2, start,
                                           start + 2, start + 3});
}
} // namespace

glm::vec3 VoxelChunk::material_color(VoxelMaterial material, bool top_face,
                                     float height_t) {
  switch (material) {
  case VoxelMaterial::Grass:
    if (top_face) {
      return glm::vec3(0.22f + height_t * 0.2f, 0.45f + height_t * 0.35f,
                       0.16f);
    }
    return glm::vec3(0.33f, 0.42f, 0.18f);
  case VoxelMaterial::Stone:
    return glm::vec3(0.46f, 0.48f, 0.5f);
  case VoxelMaterial::Dirt:
  default:
    return glm::vec3(0.38f, 0.27f, 0.18f);
  }
}

size_t VoxelChunk::index(int x, int y, int z) const {
  return static_cast<size_t>((z * CHUNK_Y * CHUNK_X) + (y * CHUNK_X) + x);
}

void VoxelChunk::generate_heightmap_terrain() {
  generate_heightmap_terrain_seeded(0x564F58554C4Cull, 0, 0);
}

void VoxelChunk::generate_heightmap_terrain_seeded(uint64_t world_seed,
                                                   int32_t chunk_x,
                                                   int32_t chunk_z) {
  voxels.fill(static_cast<uint8_t>(VoxelMaterial::Air));
  constexpr float flat_height = 6.0f;
  const float min_dim = static_cast<float>(std::min(CHUNK_X, CHUNK_Z));
  const float inner = min_dim * 0.24f;
  const float outer = min_dim * 0.39f;
  const float cx = static_cast<float>(CHUNK_X - 1) * 0.5f;
  const float cz = static_cast<float>(CHUNK_Z - 1) * 0.5f;
  const int32_t world_base_x = chunk_x * CHUNK_X;
  const int32_t world_base_z = chunk_z * CHUNK_Z;
  const WorldGenerator generator(world_seed);

  for (int z = 0; z < CHUNK_Z; ++z) {
    for (int x = 0; x < CHUNK_X; ++x) {
      const float world_x = static_cast<float>(world_base_x + x);
      const float world_z = static_cast<float>(world_base_z + z);
      float h = generator.sample_height(world_x, world_z);
      const float dx = static_cast<float>(x) - cx;
      const float dz = static_cast<float>(z) - cz;
      const float ring_d = std::max(std::fabs(dx), std::fabs(dz));
      if (ring_d <= outer) {
        const float t = std::clamp(
            (ring_d - inner) / std::max(0.001f, outer - inner), 0.0f, 1.0f);
        h = flat_height + (h - flat_height) * t;
      }
      int max_y = static_cast<int>(h);
      if (max_y < 1) {
        max_y = 1;
      }
      if (max_y >= CHUNK_Y) {
        max_y = CHUNK_Y - 1;
      }
      for (int y = 0; y <= max_y; ++y) {
        voxels[index(x, y, z)] = static_cast<uint8_t>(VoxelMaterial::Dirt);
      }
    }
  }

  refresh_surface_materials();
}

void VoxelChunk::generate_spherical_planet_seeded(uint64_t world_seed) {
  voxels.fill(static_cast<uint8_t>(VoxelMaterial::Air));
  const WorldGenerator generator(world_seed);

  const float cx = static_cast<float>(CHUNK_X - 1) * 0.5f;
  const float cy = static_cast<float>(CHUNK_Y - 1) * 0.42f;
  const float cz = static_cast<float>(CHUNK_Z - 1) * 0.5f;
  const float base_radius =
      static_cast<float>(std::min({CHUNK_X, CHUNK_Y, CHUNK_Z})) * 0.34f;
  const float shell_min = base_radius * 0.24f;

  for (int z = 0; z < CHUNK_Z; ++z) {
    for (int y = 0; y < CHUNK_Y; ++y) {
      for (int x = 0; x < CHUNK_X; ++x) {
        const float dx = static_cast<float>(x) - cx;
        const float dy = static_cast<float>(y) - cy;
        const float dz = static_cast<float>(z) - cz;
        const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (dist <= 0.001f) {
          voxels[index(x, y, z)] = static_cast<uint8_t>(VoxelMaterial::Stone);
          continue;
        }

        const float noise_a =
            generator.sample_height(cx + dx * 1.9f, cz + dz * 1.9f) - 6.0f;
        const float noise_b =
            std::sin(dy * 0.43f +
                     static_cast<float>(world_seed & 0xffu) * 0.03f) *
            0.9f;
        const float radial_noise = noise_a * 0.22f + noise_b;
        const float shell_radius = base_radius + radial_noise;

        if (dist <= shell_radius && dist >= shell_min) {
          voxels[index(x, y, z)] = static_cast<uint8_t>(VoxelMaterial::Stone);
        }
      }
    }
  }

  // Ensure a stable spawnable cap near the top hemisphere.
  const int spawn_cap_y = std::clamp(
      static_cast<int>(std::floor(cy + base_radius - 1.5f)), 1, CHUNK_Y - 2);
  for (int z = static_cast<int>(cz) - 3; z <= static_cast<int>(cz) + 3; ++z) {
    for (int x = static_cast<int>(cx) - 3; x <= static_cast<int>(cx) + 3; ++x) {
      if (x < 1 || z < 1 || x >= CHUNK_X - 1 || z >= CHUNK_Z - 1) {
        continue;
      }
      voxels[index(x, spawn_cap_y, z)] = static_cast<uint8_t>(VoxelMaterial::Dirt);
      for (int y = spawn_cap_y + 1; y < CHUNK_Y; ++y) {
        voxels[index(x, y, z)] = static_cast<uint8_t>(VoxelMaterial::Air);
      }
    }
  }

  refresh_surface_materials();
}

void VoxelChunk::generate_flat_ground(int ground_y) {
  voxels.fill(static_cast<uint8_t>(VoxelMaterial::Air));
  const int max_y = std::clamp(ground_y, 0, CHUNK_Y - 1);
  for (int z = 0; z < CHUNK_Z; ++z) {
    for (int x = 0; x < CHUNK_X; ++x) {
      for (int y = 0; y <= max_y; ++y) {
        voxels[index(x, y, z)] = static_cast<uint8_t>(VoxelMaterial::Dirt);
      }
    }
  }

  refresh_surface_materials();
}

VoxelMaterial VoxelChunk::material(int x, int y, int z) const {
  if (x < 0 || y < 0 || z < 0 || x >= CHUNK_X || y >= CHUNK_Y || z >= CHUNK_Z) {
    return VoxelMaterial::Air;
  }
  return static_cast<VoxelMaterial>(voxels[index(x, y, z)]);
}

bool VoxelChunk::solid(int x, int y, int z) const {
  return material(x, y, z) != VoxelMaterial::Air;
}

void VoxelChunk::set_material(int x, int y, int z, VoxelMaterial material_value) {
  if (x < 0 || y < 0 || z < 0 || x >= CHUNK_X || y >= CHUNK_Y || z >= CHUNK_Z) {
    return;
  }
  voxels[index(x, y, z)] = static_cast<uint8_t>(material_value);
}

void VoxelChunk::set_solid(int x, int y, int z, bool value) {
  set_material(x, y, z, value ? VoxelMaterial::Dirt : VoxelMaterial::Air);
}

void VoxelChunk::refresh_surface_materials() {
  for (int z = 0; z < CHUNK_Z; ++z) {
    for (int x = 0; x < CHUNK_X; ++x) {
      bool surface_found = false;
      for (int y = CHUNK_Y - 1; y >= 0; --y) {
        VoxelMaterial mat = material(x, y, z);
        if (mat == VoxelMaterial::Air) {
          continue;
        }

        if (!surface_found) {
          if (mat != VoxelMaterial::Stone) {
            set_material(x, y, z, VoxelMaterial::Grass);
          }
          surface_found = true;
        } else if (mat == VoxelMaterial::Grass) {
          set_material(x, y, z, VoxelMaterial::Dirt);
        }
      }
    }
  }
}

RenderMesh VoxelChunk::build_greedy_mesh(const glm::vec3 &origin,
                                         float voxel_scale) const {
  RenderMesh mesh;
  constexpr int dims[3] = {CHUNK_X, CHUNK_Y, CHUNK_Z};
  constexpr int axis_u[3] = {1, 2, 0};
  constexpr int axis_v[3] = {2, 0, 1};
  const size_t max_mask_size = static_cast<size_t>(
      std::max({CHUNK_X * CHUNK_Y, CHUNK_Y * CHUNK_Z, CHUNK_X * CHUNK_Z}));
  std::vector<int16_t> mask(max_mask_size, 0);

  // Track per-material quad counts to determine the primary material.
  uint32_t material_quads[4] = {0, 0, 0, 0};

  for (int d = 0; d < 3; ++d) {
    const int u = axis_u[d];
    const int v = axis_v[d];
    glm::ivec3 x(0);
    glm::ivec3 q(0);
    q[d] = 1;

    for (x[d] = -1; x[d] < dims[d];) {
      int n = 0;
      for (x[v] = 0; x[v] < dims[v]; ++x[v]) {
        for (x[u] = 0; x[u] < dims[u]; ++x[u], ++n) {
          const VoxelMaterial a =
              x[d] >= 0 ? material(x.x, x.y, x.z) : VoxelMaterial::Air;
          const VoxelMaterial b = x[d] < (dims[d] - 1)
                                      ? material(x.x + q.x, x.y + q.y, x.z + q.z)
                                      : VoxelMaterial::Air;

          if (a == b) {
            mask[static_cast<size_t>(n)] = 0;
          } else {
            mask[static_cast<size_t>(n)] =
                a != VoxelMaterial::Air
                    ? static_cast<int16_t>(a)
                    : static_cast<int16_t>(-static_cast<int16_t>(b));
          }
        }
      }

      ++x[d];
      n = 0;
      for (int j = 0; j < dims[v]; ++j) {
        for (int i = 0; i < dims[u];) {
          const int16_t face = mask[static_cast<size_t>(n)];
          if (face == 0) {
            ++i;
            ++n;
            continue;
          }

          int width = 1;
          while ((i + width) < dims[u] &&
                 mask[static_cast<size_t>(n + width)] == face) {
            ++width;
          }

          int height = 1;
          bool done = false;
          while ((j + height) < dims[v] && !done) {
            for (int k = 0; k < width; ++k) {
              if (mask[static_cast<size_t>(n + k + height * dims[u])] != face) {
                done = true;
                break;
              }
            }
            if (!done) {
              ++height;
            }
          }

          glm::ivec3 base(0);
          base[d] = x[d];
          base[u] = i;
          base[v] = j;

          glm::ivec3 du(0);
          glm::ivec3 dv(0);
          du[u] = width;
          dv[v] = height;

          const bool positive_face = face > 0;
          const VoxelMaterial face_material =
              static_cast<VoxelMaterial>(std::abs(face));
          ++material_quads[static_cast<uint8_t>(face_material)];
          const float surface_y =
              static_cast<float>(std::max(0, positive_face ? x[d] - 1 : x[d]));
          const float height_t =
              std::clamp(surface_y / static_cast<float>(CHUNK_Y), 0.0f, 1.0f);
          const bool top_face = d == 1 && positive_face;
          const glm::vec3 color =
              VoxelChunk::material_color(face_material, top_face, height_t);

          append_greedy_quad(mesh, origin, base, q, du, dv, positive_face, color,
                             voxel_scale);

          for (int row = 0; row < height; ++row) {
            for (int col = 0; col < width; ++col) {
              mask[static_cast<size_t>(n + col + row * dims[u])] = 0;
            }
          }

          i += width;
          n += width;
        }
      }
    }
  }

  // Set the primary material to the most-frequent face material.
  uint8_t primary = 0;
  uint32_t best = 0;
  for (uint8_t m = 1; m < 4; ++m) {
    if (material_quads[m] > best) {
      best = material_quads[m];
      primary = m;
    }
  }
  mesh.material = primary;

  return mesh;
}

RenderMesh VoxelChunk::build_sky_placeholder(float size) const {
  RenderMesh mesh;
  const float h = size * 0.5f;

  glm::vec3 top(0.18f, 0.32f, 0.6f);
  glm::vec3 horizon(0.46f, 0.62f, 0.86f);

  uint32_t start = static_cast<uint32_t>(mesh.vertices.size());
  mesh.vertices.push_back({{-h, h, -h}, top});
  mesh.vertices.push_back({{h, h, -h}, top});
  mesh.vertices.push_back({{h, -h, -h}, horizon});
  mesh.vertices.push_back({{-h, -h, -h}, horizon});
  mesh.indices.insert(mesh.indices.end(), {start, start + 3, start + 2, start,
                                           start + 2, start + 1});


  return mesh;
}
