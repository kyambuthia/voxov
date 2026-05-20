#include "engine_world/planet_terrain.hpp"

#include "engine_world/planet_math.hpp"
#include "engine_world/voxel_chunk.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

namespace {
// Temporary terrain mesh budget tunables while planet chunk streaming matures.
constexpr int k_planet_chunk_size_x = 8;
constexpr int k_planet_chunk_size_z = 8;
constexpr int k_planet_chunk_height = VoxelChunk::CHUNK_Y;
constexpr int k_planet_max_terrain_height = 28;
constexpr int k_planet_reserved_faces_per_column = 8;

uint32_t hash_u32(uint32_t value) {
  value ^= value >> 16u;
  value *= 0x7feb352du;
  value ^= value >> 15u;
  value *= 0x846ca68bu;
  value ^= value >> 16u;
  return value;
}

uint32_t hash_columns(int32_t x, int32_t z, uint64_t seed, uint32_t salt) {
  uint32_t value = static_cast<uint32_t>(x) * 0x8da6b343u;
  value ^= static_cast<uint32_t>(z) * 0xd8163841u;
  value ^= static_cast<uint32_t>(seed);
  value ^= static_cast<uint32_t>(seed >> 32u) * 0xcb1ab31fu;
  value ^= salt;
  return hash_u32(value);
}

int block_noise(int32_t world_x, int32_t world_z, uint64_t seed,
                int32_t cell_size, uint32_t salt, int amplitude) {
  const int32_t cell_x = world_x >= 0 ? world_x / cell_size
                                      : (world_x - cell_size + 1) / cell_size;
  const int32_t cell_z = world_z >= 0 ? world_z / cell_size
                                      : (world_z - cell_size + 1) / cell_size;
  const uint32_t hash = hash_columns(cell_x, cell_z, seed, salt);
  return static_cast<int>(hash % static_cast<uint32_t>(amplitude * 2 + 1)) -
         amplitude;
}

int terrain_height(int32_t world_x, int32_t world_z, uint64_t seed) {
  int height = 12;
  height += block_noise(world_x, world_z, seed, 24, 0x6d2b79f5u, 7);
  height += block_noise(world_x, world_z, seed, 8, 0x1b56c4e9u, 4);
  height += block_noise(world_x, world_z, seed, 3, 0xa511e9b3u, 2);

  const uint32_t mesa_hash = hash_columns(world_x / 11, world_z / 11, seed,
                                          0x4f1bbcdu);
  if ((mesa_hash & 15u) == 0u) {
    height += 8;
  } else if ((mesa_hash & 31u) == 1u) {
    height -= 7;
  }

  height = (height / 2) * 2;
  return std::clamp(height, 2,
                    std::min(k_planet_max_terrain_height,
                             k_planet_chunk_height - 3));
}

VoxelMaterial column_material(int32_t world_x, int32_t world_z, int voxel_y,
                              int max_y_in_column, uint64_t seed) {
  if (voxel_y < max_y_in_column - 5) {
    return VoxelMaterial::Stone;
  }

  const uint32_t hash = hash_columns(world_x, world_z, seed, 0x91e10da5u);
  if (voxel_y == max_y_in_column && max_y_in_column >= 23) {
    return VoxelMaterial::Stone;
  }
  if (voxel_y == max_y_in_column && (hash % 19u) == 0u) {
    return VoxelMaterial::Stone;
  }
  if (voxel_y >= max_y_in_column - 2) {
    return VoxelMaterial::Grass;
  }
  return VoxelMaterial::Dirt;
}

glm::vec3 terrain_color(VoxelMaterial material, int32_t world_x,
                        int32_t world_z, int voxel_y, int max_y_in_column,
                        const glm::ivec3 &face_normal) {
  const float height_t = std::clamp(
      static_cast<float>(voxel_y) / static_cast<float>(k_planet_chunk_height),
      0.0f, 1.0f);
  glm::vec3 color = VoxelChunk::material_color(material, face_normal.y > 0,
                                               height_t);
  if (material == VoxelMaterial::Grass && face_normal.y <= 0) {
    color = glm::vec3(0.42f, 0.30f, 0.16f);
  } else if (material == VoxelMaterial::Stone) {
    color = glm::vec3(0.42f + height_t * 0.24f, 0.44f + height_t * 0.22f,
                      0.45f + height_t * 0.18f);
  }

  const uint32_t hash = hash_columns(world_x, world_z, 0x56584f56u,
                                     0xb5297a4du);
  const float column_variation =
      0.82f + static_cast<float>(hash & 0xffu) * (0.28f / 255.0f);
  const float face_light = face_normal.y > 0   ? 1.10f
                           : face_normal.x != 0 ? 0.82f
                                                 : 0.92f;
  if (voxel_y == max_y_in_column && ((hash >> 8u) & 7u) == 0u) {
    color += glm::vec3(0.08f, 0.07f, 0.02f);
  }
  return glm::clamp(color * column_variation * face_light, glm::vec3(0.0f),
                    glm::vec3(1.0f));
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

void generate_heightfield(VoxelChunk &chunk, const PlanetChunkId &chunk_id,
                          uint64_t seed) {
  clear_chunk(chunk);

  for (int z = 0; z < k_planet_chunk_size_z; ++z) {
    for (int x = 0; x < k_planet_chunk_size_x; ++x) {
      const int32_t world_x = chunk_id.x * k_planet_chunk_size_x + x;
      const int32_t world_z = chunk_id.y * k_planet_chunk_size_z + z;
      const int max_y = terrain_height(world_x, world_z, seed);
      for (int y = 0; y <= max_y; ++y) {
        chunk.set_material(x, y, z,
                           column_material(world_x, world_z, y, max_y, seed));
      }
    }
  }
}

glm::vec3 remap_local_vertex(const PlanetDefinition &planet,
                             const PlanetChunkId &chunk_id,
                             const glm::vec3 &local) {
  const int32_t chunks_per_face = std::max(1, planet.chunks_per_face);
  const double face_span = 2.0 / static_cast<double>(chunks_per_face);
  const double u = -1.0 + face_span *
                              (static_cast<double>(chunk_id.x) +
                                   static_cast<double>(local.x) /
                                   static_cast<double>(k_planet_chunk_size_x));
  const double v = -1.0 + face_span *
                              (static_cast<double>(chunk_id.y) +
                               static_cast<double>(local.z) /
                                   static_cast<double>(k_planet_chunk_size_z));
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
                                   static_cast<double>(k_planet_chunk_size_x));
  const double v = -1.0 + face_span *
                              (static_cast<double>(chunk_id.y) +
                               static_cast<double>(local.z) /
                                   static_cast<double>(k_planet_chunk_size_z));
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
  generate_heightfield(chunk, chunk_id, planet.seed);
  stitch_face_edges(chunk, chunk_id.face, chunk_id.x, chunk_id.y, planet);

  RenderMesh mesh{};
  mesh.vertices.reserve(static_cast<size_t>(k_planet_chunk_size_x *
                                            k_planet_chunk_size_z *
                                            k_planet_reserved_faces_per_column *
                                            4));
  mesh.indices.reserve(static_cast<size_t>(k_planet_chunk_size_x *
                                           k_planet_chunk_size_z *
                                           k_planet_reserved_faces_per_column *
                                           6));

  // Precompute max solid Y per column for top-face detection
  int max_y_per_column[k_planet_chunk_size_x][k_planet_chunk_size_z]{};
  for (int z = 0; z < k_planet_chunk_size_z; ++z) {
    for (int x = 0; x < k_planet_chunk_size_x; ++x) {
      int max_y = 0;
      for (int y = 0; y < k_planet_chunk_height; ++y) {
        if (chunk.solid(x, y, z)) max_y = y;
      }
      max_y_per_column[x][z] = max_y;
    }
  }

  for (int z = 0; z < k_planet_chunk_size_z; ++z) {
    for (int y = 0; y < k_planet_chunk_height; ++y) {
      for (int x = 0; x < k_planet_chunk_size_x; ++x) {
        if (!chunk.solid(x, y, z)) {
          continue;
        }

        const glm::ivec3 voxel(x, y, z);
        const int32_t world_x = chunk_id.x * k_planet_chunk_size_x + x;
        const int32_t world_z = chunk_id.y * k_planet_chunk_size_z + z;
        const int max_y = max_y_per_column[x][z];
        for (const VoxelFaceDef &face : k_voxel_faces) {
          const glm::ivec3 neighbor = voxel + face.neighbor;
          bool neighbor_solid = chunk.solid(neighbor.x, neighbor.y, neighbor.z);
          if (!neighbor_solid &&
              (neighbor.x < 0 || neighbor.x >= k_planet_chunk_size_x ||
               neighbor.z < 0 || neighbor.z >= k_planet_chunk_size_z) &&
              neighbor.y >= 0 && neighbor.y < k_planet_chunk_height) {
            const int neighbor_height =
                terrain_height(world_x + face.neighbor.x,
                               world_z + face.neighbor.z, planet.seed);
            neighbor_solid = neighbor.y <= neighbor_height;
          }
          if (neighbor_solid) {
            continue;
          }
          const glm::vec3 color =
              terrain_color(chunk.material(x, y, z), world_x, world_z, y,
                            max_y, face.neighbor);
          append_planet_voxel_face(mesh, planet, chunk_id, voxel, face, color);
        }
      }
    }
  }

  mesh.mesh_id = 0x5658504c54455252ull;
  mesh.material = static_cast<uint8_t>(VoxelMaterial::Grass);
  return mesh;
}
