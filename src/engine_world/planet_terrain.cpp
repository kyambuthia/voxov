#include "engine_world/planet_terrain.hpp"

#include "engine_world/planet_math.hpp"
#include "engine_world/voxel_chunk.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

namespace {
// Temporary terrain mesh budget tunables while planet chunk streaming matures.
constexpr int k_planet_chunk_size_x = 16;
constexpr int k_planet_chunk_size_z = 16;
constexpr int k_planet_chunk_height = VoxelChunk::CHUNK_Y;
constexpr int k_planet_max_terrain_height = 30;
constexpr int k_planet_reserved_faces_per_column = 10;
constexpr int k_max_lod_shift = 30;
constexpr double k_mesh_face_edge_epsilon = 1.0e-5;

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

float smoothstep(float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

float lerp(float a, float b, float t) { return a + (b - a) * t; }

float value_noise(int32_t world_x, int32_t world_z, uint64_t seed,
                  int32_t cell_size, uint32_t salt) {
  const float fx = static_cast<float>(world_x) / static_cast<float>(cell_size);
  const float fz = static_cast<float>(world_z) / static_cast<float>(cell_size);
  const int32_t x0 = static_cast<int32_t>(std::floor(fx));
  const int32_t z0 = static_cast<int32_t>(std::floor(fz));
  const float tx = smoothstep(fx - static_cast<float>(x0));
  const float tz = smoothstep(fz - static_cast<float>(z0));

  auto sample = [&](int32_t x, int32_t z) {
    const uint32_t hash = hash_columns(x, z, seed, salt);
    return static_cast<float>(hash & 0xffffu) / 32767.5f - 1.0f;
  };

  const float a = lerp(sample(x0, z0), sample(x0 + 1, z0), tx);
  const float b = lerp(sample(x0, z0 + 1), sample(x0 + 1, z0 + 1), tx);
  return lerp(a, b, tz);
}

int terrain_height(int32_t world_x, int32_t world_z, uint64_t seed) {
  float height = 12.0f;
  height += value_noise(world_x, world_z, seed, 48, 0x6d2b79f5u) * 8.0f;
  height += value_noise(world_x, world_z, seed, 18, 0x1b56c4e9u) * 4.0f;
  height += value_noise(world_x, world_z, seed, 7, 0xa511e9b3u) * 1.5f;

  const float plateau = value_noise(world_x, world_z, seed, 64, 0x4f1bbcdu);
  if (plateau > 0.68f) {
    height =
        lerp(height, 22.0f, std::clamp((plateau - 0.68f) * 2.5f, 0.0f, 1.0f));
  } else if (plateau < -0.72f) {
    height =
        lerp(height, 5.0f, std::clamp((-0.72f - plateau) * 2.0f, 0.0f, 1.0f));
  }

  const int quantized = static_cast<int>(std::round(height));
  return std::clamp(
      quantized, 2,
      std::min(k_planet_max_terrain_height, k_planet_chunk_height - 3));
}

int32_t configured_columns_per_face(const PlanetDefinition &planet) {
  const int32_t chunks_per_face = std::max(1, planet.chunks_per_face);
  return chunks_per_face * k_planet_chunk_size_x;
}

struct TerrainColumnSample {
  int32_t x = 0;
  int32_t z = 0;
  int height = 0;
};

TerrainColumnSample sample_terrain_column_at_face_uv(
    const PlanetDefinition &planet, double u, double v) {
  const int32_t columns_per_axis = configured_columns_per_face(planet);
  const double column_x = (std::clamp(u, -1.0, 1.0) + 1.0) * 0.5 *
                          static_cast<double>(columns_per_axis);
  const double column_z = (std::clamp(v, -1.0, 1.0) + 1.0) * 0.5 *
                          static_cast<double>(columns_per_axis);
  TerrainColumnSample sample{};
  sample.x = std::clamp(static_cast<int32_t>(std::floor(column_x)), 0,
                        columns_per_axis - 1);
  sample.z = std::clamp(static_cast<int32_t>(std::floor(column_z)), 0,
                        columns_per_axis - 1);
  sample.height = terrain_height(sample.x, sample.z, planet.seed);
  return sample;
}

int terrain_height_at_face_uv(const PlanetDefinition &planet, double u,
                              double v) {
  return sample_terrain_column_at_face_uv(planet, u, v).height;
}

glm::dvec2 chunk_local_column_center_uv(const PlanetChunkUvRange &range,
                                        double local_x, double local_z) {
  const double tx =
      (local_x + 0.5) / static_cast<double>(k_planet_chunk_size_x);
  const double tz =
      (local_z + 0.5) / static_cast<double>(k_planet_chunk_size_z);
  return glm::dvec2(range.u0 + (range.u1 - range.u0) * tx,
                    range.v0 + (range.v1 - range.v0) * tz);
}

TerrainColumnSample sample_chunk_local_column(const PlanetDefinition &planet,
                                              const PlanetChunkUvRange &range,
                                              double local_x, double local_z) {
  const glm::dvec2 uv = chunk_local_column_center_uv(range, local_x, local_z);
  return sample_terrain_column_at_face_uv(planet, uv.x, uv.y);
}

double clamp_mesh_face_uv(double value) {
  return std::clamp(value, -1.0 + k_mesh_face_edge_epsilon,
                    1.0 - k_mesh_face_edge_epsilon);
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
  glm::vec3 color =
      VoxelChunk::material_color(material, face_normal.y > 0, height_t);
  if (material == VoxelMaterial::Grass && face_normal.y <= 0) {
    color = glm::vec3(0.42f, 0.30f, 0.16f);
  } else if (material == VoxelMaterial::Stone) {
    color = glm::vec3(0.42f + height_t * 0.24f, 0.44f + height_t * 0.22f,
                      0.45f + height_t * 0.18f);
  }

  const uint32_t hash =
      hash_columns(world_x, world_z, 0x56584f56u, 0xb5297a4du);
  const float column_variation =
      0.82f + static_cast<float>(hash & 0xffu) * (0.28f / 255.0f);
  const float face_light = face_normal.y > 0    ? 1.10f
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

void generate_heightfield(VoxelChunk &chunk, const PlanetDefinition &planet,
                          const PlanetChunkUvRange &range) {
  clear_chunk(chunk);

  for (int z = 0; z < k_planet_chunk_size_z; ++z) {
    for (int x = 0; x < k_planet_chunk_size_x; ++x) {
      const TerrainColumnSample sample =
          sample_chunk_local_column(planet, range, x, z);
      const int max_y = sample.height;
      for (int y = 0; y <= max_y; ++y) {
        chunk.set_material(
            x, y, z,
            column_material(sample.x, sample.z, y, max_y, planet.seed));
      }
    }
  }
}

glm::vec3 remap_local_vertex(const PlanetDefinition &planet,
                             const PlanetChunkId &chunk_id,
                             const PlanetChunkUvRange &range,
                             const glm::vec3 &local,
                             PlanetTerrainRenderMode mode,
                             const PlanetSurfaceRenderFrame &surface_frame) {
  const double u = clamp_mesh_face_uv(
      range.u0 +
      (range.u1 - range.u0) * (static_cast<double>(local.x) /
                               static_cast<double>(k_planet_chunk_size_x)));
  const double v = clamp_mesh_face_uv(
      range.v0 +
      (range.v1 - range.v0) * (static_cast<double>(local.z) /
                               static_cast<double>(k_planet_chunk_size_z)));
  const double height = static_cast<double>(local.y) * planet.voxel_size;

  if (mode == PlanetTerrainRenderMode::SurfaceFlatFace) {
    const int32_t columns_per_face = configured_columns_per_face(planet);
    const int32_t lod = std::clamp(chunk_id.lod, 0, k_max_lod_shift);
    const int32_t cells = int32_t{1} << lod;
    const double cell_columns =
        static_cast<double>(columns_per_face) / static_cast<double>(cells);
    const double local_face_x =
        (static_cast<double>(chunk_id.x) * cell_columns) +
        static_cast<double>(local.x);
    const double local_face_z =
        (static_cast<double>(chunk_id.y) * cell_columns) +
        static_cast<double>(local.z);
    LocalFaceVoxelCoords coords{};
    coords.face = chunk_id.face;
    coords.xyz =
        glm::dvec3(local_face_x * planet.voxel_size, height,
                   local_face_z * planet.voxel_size);

    const double scale = std::max(surface_frame.distortion_scale, 1.0e-9);
    const glm::dvec3 local_pos =
        (coords.xyz - surface_frame.camera_local_origin) * scale;
    return glm::vec3(local_pos);
  }

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

glm::vec3 sphere_normal_for_local(const PlanetChunkId &chunk_id,
                                  const PlanetChunkUvRange &range,
                                  const glm::vec3 &local) {
  const double u = clamp_mesh_face_uv(
      range.u0 +
      (range.u1 - range.u0) * (static_cast<double>(local.x) /
                               static_cast<double>(k_planet_chunk_size_x)));
  const double v = clamp_mesh_face_uv(
      range.v0 +
      (range.v1 - range.v0) * (static_cast<double>(local.z) /
                               static_cast<double>(k_planet_chunk_size_z)));
  return glm::vec3(face_uv_to_direction(chunk_id.face, u, v));
}

void append_planet_voxel_face(RenderMesh &mesh, const PlanetDefinition &planet,
                              const PlanetChunkId &chunk_id,
                              const PlanetChunkUvRange &range,
                              const glm::ivec3 &voxel, const VoxelFaceDef &face,
                              const glm::vec3 &color,
                              PlanetTerrainRenderMode mode,
                              const PlanetSurfaceRenderFrame &surface_frame) {
  std::array<glm::vec3, 4> positions{};
  glm::vec3 local_center(0.0f);
  for (size_t i = 0; i < face.corners.size(); ++i) {
    const glm::vec3 local = glm::vec3(voxel) + face.corners[i];
    positions[i] =
        remap_local_vertex(planet, chunk_id, range, local, mode, surface_frame);
    local_center += local;
  }
  local_center *= 0.25f;

  glm::vec3 normal = sphere_normal_for_local(chunk_id, range, local_center);
  if (mode == PlanetTerrainRenderMode::SurfaceFlatFace) {
    normal = glm::vec3(face.neighbor);
  }
  const glm::vec3 expected =
      remap_local_vertex(planet, chunk_id, range,
                         local_center + glm::vec3(face.neighbor) * 0.5f, mode,
                         surface_frame) -
      remap_local_vertex(planet, chunk_id, range,
                         local_center - glm::vec3(face.neighbor) * 0.5f, mode,
                         surface_frame);
  const glm::vec3 geometric =
      glm::cross(positions[1] - positions[0], positions[2] - positions[0]);
  const bool flip = glm::dot(geometric, expected) < 0.0f;

  const uint32_t start = static_cast<uint32_t>(mesh.vertices.size());
  for (const glm::vec3 &position : positions) {
    mesh.vertices.push_back({position, color, normal});
  }

  if (!flip) {
    mesh.indices.insert(mesh.indices.end(), {start, start + 1, start + 2, start,
                                             start + 2, start + 3});
  } else {
    mesh.indices.insert(mesh.indices.end(), {start, start + 2, start + 1, start,
                                             start + 3, start + 2});
  }
}
} // namespace

PlanetChunkUvRange planet_chunk_uv_range(const PlanetChunkId &chunk_id) {
  const int32_t lod = std::clamp(chunk_id.lod, 0, k_max_lod_shift);
  const int32_t cells = int32_t{1} << lod;
  const int32_t cell_x = std::clamp(chunk_id.x, 0, cells - 1);
  const int32_t cell_y = std::clamp(chunk_id.y, 0, cells - 1);
  const double span = 2.0 / static_cast<double>(cells);

  PlanetChunkUvRange range{};
  range.u0 = -1.0 + span * static_cast<double>(cell_x);
  range.v0 = -1.0 + span * static_cast<double>(cell_y);
  range.u1 = range.u0 + span;
  range.v1 = range.v0 + span;
  return range;
}

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

RenderMesh build_planet_terrain_mesh(
    const PlanetDefinition &planet, const PlanetChunkId &chunk_id,
    PlanetTerrainRenderMode mode,
    const PlanetSurfaceRenderFrame &surface_frame) {
  const PlanetChunkUvRange uv_range = planet_chunk_uv_range(chunk_id);

  VoxelChunk chunk;
  generate_heightfield(chunk, planet, uv_range);
  stitch_face_edges(chunk, chunk_id.face, chunk_id.x, chunk_id.y, planet);

  RenderMesh mesh{};
  mesh.vertices.reserve(
      static_cast<size_t>(k_planet_chunk_size_x * k_planet_chunk_size_z *
                          k_planet_reserved_faces_per_column * 4));
  mesh.indices.reserve(
      static_cast<size_t>(k_planet_chunk_size_x * k_planet_chunk_size_z *
                          k_planet_reserved_faces_per_column * 6));

  // Precompute configured terrain samples per local column for top-face
  // detection and deterministic material/color variation.
  int max_y_per_column[k_planet_chunk_size_x][k_planet_chunk_size_z]{};
  int32_t sample_x_per_column[k_planet_chunk_size_x][k_planet_chunk_size_z]{};
  int32_t sample_z_per_column[k_planet_chunk_size_x][k_planet_chunk_size_z]{};
  for (int z = 0; z < k_planet_chunk_size_z; ++z) {
    for (int x = 0; x < k_planet_chunk_size_x; ++x) {
      const TerrainColumnSample sample =
          sample_chunk_local_column(planet, uv_range, x, z);
      max_y_per_column[x][z] = sample.height;
      sample_x_per_column[x][z] = sample.x;
      sample_z_per_column[x][z] = sample.z;
    }
  }

  for (int z = 0; z < k_planet_chunk_size_z; ++z) {
    for (int y = 0; y < k_planet_chunk_height; ++y) {
      for (int x = 0; x < k_planet_chunk_size_x; ++x) {
        if (!chunk.solid(x, y, z)) {
          continue;
        }

        const glm::ivec3 voxel(x, y, z);
        const int32_t world_x = sample_x_per_column[x][z];
        const int32_t world_z = sample_z_per_column[x][z];
        const int max_y = max_y_per_column[x][z];
        for (const VoxelFaceDef &face : k_voxel_faces) {
          const glm::ivec3 neighbor = voxel + face.neighbor;
          bool neighbor_solid = chunk.solid(neighbor.x, neighbor.y, neighbor.z);
          if (!neighbor_solid &&
              (neighbor.x < 0 || neighbor.x >= k_planet_chunk_size_x ||
               neighbor.z < 0 || neighbor.z >= k_planet_chunk_size_z) &&
              neighbor.y >= 0 && neighbor.y < k_planet_chunk_height) {
            const glm::dvec2 neighbor_uv =
                chunk_local_column_center_uv(uv_range, neighbor.x, neighbor.z);
            if (neighbor_uv.x >= -1.0 && neighbor_uv.x <= 1.0 &&
                neighbor_uv.y >= -1.0 && neighbor_uv.y <= 1.0) {
              const int neighbor_height = terrain_height_at_face_uv(
                  planet, neighbor_uv.x, neighbor_uv.y);
              neighbor_solid = neighbor.y <= neighbor_height;
            }
            // Cross-face neighbor lookup needs the cube-face transform for the
            // adjacent face. Until that is wired in, outer cube-face edges emit
            // boundary faces instead of culling against wrapped terrain.
          }
          if (neighbor_solid) {
            continue;
          }
          const glm::vec3 color =
              terrain_color(chunk.material(x, y, z), world_x, world_z, y, max_y,
                            face.neighbor);
          append_planet_voxel_face(mesh, planet, chunk_id, uv_range, voxel,
                                   face, color, mode, surface_frame);
        }
      }
    }
  }

  mesh.mesh_id = 0x5658504c54455252ull;
  mesh.material = static_cast<uint8_t>(VoxelMaterial::Grass);
  return mesh;
}

RenderMesh build_single_face_planet_terrain_mesh(
    const PlanetDefinition &planet, const PlanetChunkId &chunk_id) {
  return build_planet_terrain_mesh(
      planet, chunk_id, PlanetTerrainRenderMode::SpaceCubedSphere, {});
}

double planet_terrain_max_height_above_base(const PlanetDefinition &planet) {
  const int32_t columns_per_axis = configured_columns_per_face(planet);
  int max_height_voxels = 0;

  for (int32_t z = 0; z < columns_per_axis; ++z) {
    for (int32_t x = 0; x < columns_per_axis; ++x) {
      max_height_voxels =
          std::max(max_height_voxels, terrain_height(x, z, planet.seed));
    }
  }

  return static_cast<double>(max_height_voxels + 1) * planet.voxel_size;
}

double planet_terrain_height_above_base_at_direction(
    const PlanetDefinition &planet, const glm::dvec3 &direction) {
  const PlanetFaceUV uv = direction_to_face_uv(direction);
  return static_cast<double>(terrain_height_at_face_uv(planet, uv.u, uv.v) +
                             1) *
         planet.voxel_size;
}
