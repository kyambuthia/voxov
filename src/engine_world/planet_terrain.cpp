#include "engine_world/planet_terrain.hpp"

#include "engine_world/planet_math.hpp"
#include "engine_world/voxel_chunk.hpp"

#include <algorithm>
#include <cmath>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

namespace {
constexpr int k_planet_chunk_size = 16;

float terrain_height(int x, int z, uint64_t seed) {
  constexpr float base_height = 8.0f;
  const float seed_x = static_cast<float>(seed & 0xffffu) * 0.00019f;
  const float seed_z = static_cast<float>((seed >> 16u) & 0xffffu) * 0.00023f;
  const float rolling = std::sin(static_cast<float>(x) * 0.73f + seed_x) * 2.0f;
  const float ridge = std::cos(static_cast<float>(z) * 0.61f + seed_z) * 1.5f;
  const float detail =
      std::sin(static_cast<float>(x + z) * 0.47f + seed_x * 2.3f) * 0.9f;
  return std::clamp(base_height + rolling + ridge + detail, 1.0f,
                    static_cast<float>(k_planet_chunk_size - 2));
}

glm::vec3 terrain_color(float voxel_y) {
  const float t =
      std::clamp(voxel_y / static_cast<float>(k_planet_chunk_size), 0.0f, 1.0f);
  constexpr glm::vec3 low(0.42f, 0.27f, 0.13f);
  constexpr glm::vec3 mid(0.18f, 0.48f, 0.18f);
  constexpr glm::vec3 high(0.88f, 0.9f, 0.84f);

  if (t < 0.62f) {
    return glm::mix(low, mid, t / 0.62f);
  }
  return glm::mix(mid, high, (t - 0.62f) / 0.38f);
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

void rebuild_vertex_normals(RenderMesh &mesh) {
  for (RenderVertex &vertex : mesh.vertices) {
    vertex.normal = glm::vec3(0.0f);
  }

  for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
    RenderVertex &a = mesh.vertices[mesh.indices[i]];
    RenderVertex &b = mesh.vertices[mesh.indices[i + 1]];
    RenderVertex &c = mesh.vertices[mesh.indices[i + 2]];
    const glm::vec3 normal =
        glm::cross(b.position - a.position, c.position - a.position);
    const float len2 = glm::dot(normal, normal);
    if (len2 <= 1.0e-8f) {
      continue;
    }
    const glm::vec3 n = normal / std::sqrt(len2);
    a.normal += n;
    b.normal += n;
    c.normal += n;
  }

  for (RenderVertex &vertex : mesh.vertices) {
    const float len2 = glm::dot(vertex.normal, vertex.normal);
    if (len2 <= 1.0e-8f) {
      vertex.normal = glm::vec3(0.0f, 1.0f, 0.0f);
    } else {
      vertex.normal /= std::sqrt(len2);
    }
  }
}
} // namespace

RenderMesh build_single_face_planet_terrain_mesh(
    const PlanetDefinition &planet, const PlanetChunkId &chunk_id) {
  VoxelChunk chunk;
  generate_heightfield(chunk, planet.seed);

  RenderMesh mesh = chunk.build_greedy_mesh(glm::vec3(0.0f), 1.0f);
  for (RenderVertex &vertex : mesh.vertices) {
    const glm::vec3 local = vertex.position;
    vertex.position = remap_local_vertex(planet, chunk_id, local);
    vertex.color = terrain_color(local.y);
  }

  rebuild_vertex_normals(mesh);
  mesh.mesh_id = 0x5658504c54455252ull;
  mesh.material = static_cast<uint8_t>(VoxelMaterial::Grass);
  return mesh;
}
