#include "engine_world/planet_terrain.hpp"

#include "engine_world/planet_math.hpp"
#include "engine_world/voxel_chunk.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

namespace {
constexpr int k_planet_chunk_size = 16;
constexpr int k_planet_chunk_height = VoxelChunk::CHUNK_Y;

float terrain_height(int x, int z, uint64_t seed) {
  constexpr float base_height = 14.0f;
  const float seed_x = static_cast<float>(seed & 0xffffu) * 0.00019f;
  const float seed_z = static_cast<float>((seed >> 16u) & 0xffffu) * 0.00023f;
  const float rolling = std::sin(static_cast<float>(x) * 0.73f + seed_x) * 6.0f;
  const float ridge = std::cos(static_cast<float>(z) * 0.61f + seed_z) * 5.0f;
  const float detail =
      std::sin(static_cast<float>(x + z) * 0.47f + seed_x * 2.3f) * 3.0f;
  return std::clamp(base_height + rolling + ridge + detail, 1.0f,
                    static_cast<float>(k_planet_chunk_height - 2));
}

glm::vec3 terrain_color(int voxel_x, int voxel_y, int voxel_z, int max_y_in_column,
                        bool is_top_face) {
  const float column_variation =
      0.88f + 0.12f * std::sin(static_cast<float>(voxel_x * 7 + voxel_z * 11) * 0.43f);
  constexpr glm::vec3 top_color(0.22f, 0.55f, 0.18f);
  constexpr glm::vec3 side_color(0.48f, 0.35f, 0.22f);
  constexpr glm::vec3 deep_color(0.35f, 0.32f, 0.28f);

  if (voxel_y >= max_y_in_column) {
    return top_color * column_variation;
  }
  if (voxel_y >= max_y_in_column - 3) {
    return side_color * column_variation;
  }
  return deep_color * (0.85f + 0.15f * column_variation);
}

void clear_chunk(VoxelChunk &chunk) {
  for (int z = 0; z < VoxelChunk::CHUNK_Z; ++z) {
    for (int y = 0; y < VoxelChunk::CHUNK_Y; ++y) {
      for (int x = 0; x < VoxelChunk::CHUNK_X; ++x) {
        chunk.set_material(x, y, z, VoxelMaterial::Air);
      }
    }
  }
}

void generate_heightfield(VoxelChunk &chunk, uint64_t seed) {
  clear_chunk(chunk);

  for (int z = 0; z < k_planet_chunk_size; ++z) {
    for (int x = 0; x < k_planet_chunk_size; ++x) {
      const int max_y = static_cast<int>(std::floor(terrain_height(x, z, seed)));
      for (int y = 0; y <= max_y; ++y) {
        chunk.set_material(x, y, z, VoxelMaterial::Dirt);
      }
    }
  }

  chunk.refresh_surface_materials();
}

glm::vec3 remap_local_vertex(const PlanetDefinition &planet,
                             const PlanetChunkId &chunk_id,
                             const glm::vec3 &local) {
  const int32_t chunks_per_face = std::max(1, planet.chunks_per_face);
  const double face_span = 2.0 / static_cast<double>(chunks_per_face);
  const double u = -1.0 + face_span *
                              (static_cast<double>(chunk_id.x) +
                               static_cast<double>(local.x) /
                                   static_cast<double>(k_planet_chunk_size));
  const double v = -1.0 + face_span *
                              (static_cast<double>(chunk_id.y) +
                               static_cast<double>(local.z) /
                                   static_cast<double>(k_planet_chunk_size));
  const double height = static_cast<double>(local.y) * planet.voxel_size;
  return glm::vec3(voxel_world_pos(planet, chunk_id.face, u, v, height));
}

struct VoxelFaceDef {
  glm::ivec3 neighbor;
  std::array<glm::vec3, 4> corners;
};

const VoxelFaceDef k_voxel_faces[] = {
    {glm::ivec3(1, 0, 0),
     {glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(1.0f, 1.0f, 0.0f),
      glm::vec3(1.0f, 1.0f, 1.0f), glm::vec3(1.0f, 0.0f, 1.0f)}},
    {glm::ivec3(-1, 0, 0),
     {glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f),
      glm::vec3(0.0f, 1.0f, 1.0f), glm::vec3(0.0f, 1.0f, 0.0f)}},
    {glm::ivec3(0, 1, 0),
     {glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.0f, 1.0f, 0.0f),
      glm::vec3(1.0f, 1.0f, 1.0f), glm::vec3(0.0f, 1.0f, 1.0f)}},
    {glm::ivec3(0, -1, 0),
     {glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f),
      glm::vec3(1.0f, 0.0f, 1.0f), glm::vec3(1.0f, 0.0f, 0.0f)}},
    {glm::ivec3(0, 0, 1),
     {glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(1.0f, 0.0f, 1.0f),
      glm::vec3(1.0f, 1.0f, 1.0f), glm::vec3(0.0f, 1.0f, 1.0f)}},
    {glm::ivec3(0, 0, -1),
     {glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f),
      glm::vec3(1.0f, 1.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f)}}};

glm::vec3 sphere_normal_for_local(const PlanetDefinition &planet,
                                  const PlanetChunkId &chunk_id,
                                  const glm::vec3 &local) {
  const int32_t chunks_per_face = std::max(1, planet.chunks_per_face);
  const double face_span = 2.0 / static_cast<double>(chunks_per_face);
  const double u = -1.0 + face_span *
                              (static_cast<double>(chunk_id.x) +
                               static_cast<double>(local.x) /
                                   static_cast<double>(k_planet_chunk_size));
  const double v = -1.0 + face_span *
                              (static_cast<double>(chunk_id.y) +
                               static_cast<double>(local.z) /
                                   static_cast<double>(k_planet_chunk_size));
  return glm::vec3(face_uv_to_direction(chunk_id.face, u, v));
}

void append_planet_voxel_face(RenderMesh &mesh, const PlanetDefinition &planet,
                              const PlanetChunkId &chunk_id,
                              const glm::ivec3 &voxel,
                              const VoxelFaceDef &face,
                              const glm::vec3 &color) {
  std::array<glm::vec3, 4> positions{};
  glm::vec3 local_center(0.0f);
  for (size_t i = 0; i < face.corners.size(); ++i) {
    const glm::vec3 local = glm::vec3(voxel) + face.corners[i];
    positions[i] = remap_local_vertex(planet, chunk_id, local);
    local_center += local;
  }
  local_center *= 0.25f;

  const glm::vec3 normal =
      sphere_normal_for_local(planet, chunk_id, local_center);
  const glm::vec3 expected =
      remap_local_vertex(planet, chunk_id,
                         local_center + glm::vec3(face.neighbor) * 0.5f) -
      remap_local_vertex(planet, chunk_id,
                         local_center - glm::vec3(face.neighbor) * 0.5f);
  const glm::vec3 geometric =
      glm::cross(positions[1] - positions[0], positions[2] - positions[0]);
  const bool flip = glm::dot(geometric, expected) < 0.0f;

  const uint32_t start = static_cast<uint32_t>(mesh.vertices.size());
  for (const glm::vec3 &position : positions) {
    mesh.vertices.push_back({position, color, normal});
  }

  if (!flip) {
    mesh.indices.insert(mesh.indices.end(),
                        {start, start + 1, start + 2, start, start + 2,
                         start + 3});
  } else {
    mesh.indices.insert(mesh.indices.end(),
                        {start, start + 2, start + 1, start, start + 3,
                         start + 2});
  }
}
} // namespace

void stitch_face_edges(VoxelChunk &chunk, PlanetFace face, int32_t chunk_x,
                       int32_t chunk_y, const PlanetDefinition &planet) {
  (void)chunk;
  (void)face;
  (void)chunk_x;
  (void)chunk_y;
  (void)planet;
  // TODO: Implement face-edge stitching in Phase 5b
  return;
}

RenderMesh build_single_face_planet_terrain_mesh(
    const PlanetDefinition &planet, const PlanetChunkId &chunk_id) {
  VoxelChunk chunk;
  generate_heightfield(chunk, planet.seed);
  stitch_face_edges(chunk, chunk_id.face, chunk_id.x, chunk_id.y, planet);

  RenderMesh mesh{};
  mesh.vertices.reserve(static_cast<size_t>(k_planet_chunk_size *
                                            k_planet_chunk_size *
                                            k_planet_chunk_height * 6 * 4));
  mesh.indices.reserve(static_cast<size_t>(k_planet_chunk_size *
                                           k_planet_chunk_size *
                                           k_planet_chunk_height * 6 * 6));

  // Precompute max solid Y per column for top-face detection
  int max_y_per_column[16][16]{};
  for (int z = 0; z < k_planet_chunk_size; ++z) {
    for (int x = 0; x < k_planet_chunk_size; ++x) {
      int max_y = 0;
      for (int y = 0; y < k_planet_chunk_height; ++y) {
        if (chunk.solid(x, y, z)) max_y = y;
      }
      max_y_per_column[x][z] = max_y;
    }
  }

  for (int z = 0; z < k_planet_chunk_size; ++z) {
    for (int y = 0; y < k_planet_chunk_height; ++y) {
      for (int x = 0; x < k_planet_chunk_size; ++x) {
        if (!chunk.solid(x, y, z)) {
          continue;
        }

        const glm::ivec3 voxel(x, y, z);
        const int max_y = max_y_per_column[x][z];
        for (const VoxelFaceDef &face : k_voxel_faces) {
          const glm::ivec3 neighbor = voxel + face.neighbor;
          if (chunk.solid(neighbor.x, neighbor.y, neighbor.z)) {
            continue;
          }
          const bool is_top = (face.neighbor.y > 0);
          const glm::vec3 color = terrain_color(x, y, z, max_y, is_top);
          append_planet_voxel_face(mesh, planet, chunk_id, voxel, face, color);
        }
      }
    }
  }

  mesh.mesh_id = 0x5658504c54455252ull;
  mesh.material = static_cast<uint8_t>(VoxelMaterial::Grass);
  return mesh;
}
