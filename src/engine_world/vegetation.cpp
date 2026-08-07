#include "engine_world/vegetation.hpp"

#include "engine_world/planet.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace vegetation {
namespace {

uint64_t splitmix64(uint64_t value) {
    value += 0x9e3779b97f4a7c15ull;
    value = (value ^ (value >> 30u)) * 0xbf58476d1ce4e5b9ull;
    value = (value ^ (value >> 27u)) * 0x94d049bb133111ebull;
    return value ^ (value >> 31u);
}

float unit_float(uint64_t value) {
    return static_cast<float>((value >> 40u) & 0xffffffu) /
           static_cast<float>(0xffffffu);
}

uint64_t chunk_key(const BlockAddress &chunk) {
    uint64_t value = static_cast<uint64_t>(static_cast<uint8_t>(chunk.sector));
    value ^= static_cast<uint64_t>(static_cast<uint32_t>(chunk.shell)) << 8u;
    value ^= static_cast<uint64_t>(static_cast<uint32_t>(chunk.chunk.x)) << 16u;
    value ^= static_cast<uint64_t>(static_cast<uint32_t>(chunk.chunk.y)) << 32u;
    value ^= static_cast<uint64_t>(static_cast<uint32_t>(chunk.chunk.z)) << 48u;
    return value;
}

void append_billboard_quad(RenderMesh &mesh, const glm::dvec3 &base,
                           const glm::dvec3 &right,
                           const glm::dvec3 &up,
                           double width, double height,
                           uint8_t texture_layer,
                           const glm::vec3 &color,
                           const glm::dvec3 &camera_origin) {
    const glm::dvec3 tangent = glm::normalize(right);
    const glm::dvec3 vertical = glm::normalize(up);
    const glm::dvec3 normal = glm::normalize(glm::cross(tangent, vertical));
    const glm::dvec3 half_right = tangent * (width * 0.5);
    const glm::dvec3 top = base + vertical * height;
    const glm::dvec3 corners[] = {
        base - half_right,
        base + half_right,
        top + half_right,
        top - half_right,
    };
    const uint32_t start = static_cast<uint32_t>(mesh.vertices.size());
    const glm::vec3 render_normal(normal);
    const float layer = static_cast<float>(texture_layer);
    // PNG rows are sampled top-to-bottom by the texture path. The planted
    // base therefore uses V=1 and the tip uses V=0; this keeps the roots on
    // the voxel surface instead of rendering every card upside down.
    mesh.vertices.push_back({glm::vec3(corners[0] - camera_origin), color,
                             render_normal, {0.0f, 1.0f, layer}});
    mesh.vertices.push_back({glm::vec3(corners[1] - camera_origin), color,
                             render_normal, {1.0f, 1.0f, layer}});
    mesh.vertices.push_back({glm::vec3(corners[2] - camera_origin), color,
                             render_normal, {1.0f, 0.0f, layer}});
    mesh.vertices.push_back({glm::vec3(corners[3] - camera_origin), color,
                             render_normal, {0.0f, 0.0f, layer}});
    mesh.indices.insert(mesh.indices.end(),
                        {start, start + 1u, start + 2u,
                         start, start + 2u, start + 3u});
}

uint64_t content_hash(const BlockAddress &chunk,
                      const glm::dvec3 &camera_origin,
                      int32_t lod_level) {
    uint64_t hash = splitmix64(chunk_key(chunk) ^
                               (static_cast<uint64_t>(lod_level) << 56u));
    uint64_t bits = 0;
    std::memcpy(&bits, &camera_origin.x, sizeof(bits));
    hash ^= splitmix64(bits);
    std::memcpy(&bits, &camera_origin.y, sizeof(bits));
    hash ^= splitmix64(bits);
    std::memcpy(&bits, &camera_origin.z, sizeof(bits));
    hash ^= splitmix64(bits);
    return hash == 0 ? 1 : hash;
}

} // namespace

uint64_t mesh_id(const BlockAddress &chunk) {
    // Keep the terrain and vegetation IDs in separate namespaces while
    // retaining the same stable address-to-GPU-cache relationship.
    return BlockWorld::chunk_mesh_id(chunk) ^ 0x5645474554415445ull;
}

RenderMesh build_grass_mesh(const BlockWorld &world,
                            const BlockAddress &chunk,
                            const VoxelChunk &voxels,
                            const glm::dvec3 &camera_relative_origin,
                            int32_t lod_level) {
    RenderMesh mesh{};
    mesh.mesh_id = mesh_id(chunk);
    mesh.world_origin = camera_relative_origin;
    mesh.material = kBillboardVegetationMaterial;
    // The dedicated vegetation pipeline is no-cull; this flag remains false
    // so it is not mistaken for fallback geometry by other render backends.
    mesh.double_sided = false;

    const int32_t surface_shell = world.shell_count() - 1;
    if (chunk.shell != surface_shell) {
        mesh.content_hash = content_hash(chunk, camera_relative_origin,
                                         lod_level);
        return mesh;
    }

    const ShellConfig &shell = world.shell_config(surface_shell);
    const int32_t chunk_size = world.config().chunk_size;
    const int32_t stride = 1 << std::clamp(lod_level, 0, 2);
    const int32_t column_base_x = chunk.chunk.x * chunk_size;
    const int32_t column_base_z = chunk.chunk.z * chunk_size;
    const int32_t layer_base = chunk.chunk.y * chunk_size;
    const uint64_t seed = world.planet().seed ^ 0x4752415353ull;
    const glm::dvec3 planet_center = world.planet().center;

    mesh.vertices.reserve(256);
    mesh.indices.reserve(512);
    for (int32_t z = 0; z < chunk_size; z += stride) {
        for (int32_t x = 0; x < chunk_size; x += stride) {
            const int32_t sample_x = std::min(x + stride / 2, chunk_size - 1);
            const int32_t sample_z = std::min(z + stride / 2, chunk_size - 1);
            const int32_t global_x = column_base_x + sample_x;
            const int32_t global_z = column_base_z + sample_z;
            const int32_t surface_height =
                world.terrain_height_at_face_uv(chunk.sector, global_x,
                                                global_z);
            const int32_t top_y = surface_height - layer_base;
            if (top_y < 0 || top_y >= chunk_size ||
                !voxels.solid(sample_x, top_y, sample_z) ||
                voxels.material(sample_x, top_y, sample_z) !=
                    VoxelMaterial::Grass) {
                continue;
            }

            const uint64_t key = splitmix64(
                seed ^ chunk_key(chunk) ^
                (static_cast<uint64_t>(static_cast<uint32_t>(global_x)) << 17u) ^
                static_cast<uint64_t>(static_cast<uint32_t>(global_z)));
            // Broad patches keep the field from looking like uniform lawn.
            const uint64_t patch_key = splitmix64(
                seed ^ static_cast<uint64_t>(static_cast<uint32_t>(global_x / 4)) *
                    0x9e3779b9u ^
                static_cast<uint64_t>(static_cast<uint32_t>(global_z / 4)) *
                    0x85ebca6bu);
            const float patch = unit_float(patch_key);
            // The reference look is a living ground layer, not isolated lawn
            // props. Keep deterministic patches, but let them overlap into
            // visibly continuous clusters.
            const float density = 0.20f + patch * 0.30f;
            if (unit_float(key) > density) continue;

            const double u = -1.0 +
                2.0 * (static_cast<double>(global_x) + 0.5) /
                    static_cast<double>(shell.horizontal_res);
            const double v = -1.0 +
                2.0 * (static_cast<double>(global_z) + 0.5) /
                    static_cast<double>(shell.horizontal_res);
            glm::dvec3 up = face_uv_to_direction(chunk.sector, u, v);
            const glm::dvec3 east_hint =
                std::abs(up.y) > 0.98 ? glm::dvec3(1.0, 0.0, 0.0)
                                      : glm::dvec3(0.0, 1.0, 0.0);
            glm::dvec3 east = glm::normalize(glm::cross(east_hint, up));
            glm::dvec3 north = glm::normalize(glm::cross(up, east));

            // Reject extreme slopes. The small finite difference is in face
            // space, but normalization keeps the sample on the sphere.
            const double sample_step =
                2.0 / static_cast<double>(shell.horizontal_res);
            auto sample_height = [&](const glm::dvec3 &direction) {
                return world.terrain_height_at(glm::normalize(direction));
            };
            const int32_t h_east = sample_height(up + east * sample_step);
            const int32_t h_west = sample_height(up - east * sample_step);
            const int32_t h_north = sample_height(up + north * sample_step);
            const int32_t h_south = sample_height(up - north * sample_step);
            const int32_t max_slope = std::max(
                {std::abs(h_east - h_west), std::abs(h_north - h_south)});
            if (max_slope > 3) continue;

            const float jitter_x = unit_float(splitmix64(key ^ 0x11u)) - 0.5f;
            const float jitter_z = unit_float(splitmix64(key ^ 0x29u)) - 0.5f;
            const double cell_size =
                static_cast<double>(stride) * world.config().block_size;
            glm::dvec3 position =
                planet_center + up * world.surface_radial_distance(up);
            position += east * static_cast<double>(jitter_x) * cell_size * 0.32;
            position += north * static_cast<double>(jitter_z) * cell_size * 0.32;
            up = glm::normalize(position - planet_center);
            position = planet_center + up *
                (world.surface_radial_distance(up) + 0.025);

            const float type_roll = unit_float(splitmix64(key ^ 0x31u));
            const double angle = static_cast<double>(
                unit_float(splitmix64(key ^ 0x59u)) * 6.2831853f);

            auto tangent = [&](double radians) {
                return east * std::cos(radians) +
                       north * std::sin(radians);
            };
            auto billboard = [&](double radians, double width,
                                  double height, uint8_t texture_layer,
                                  const glm::vec3 &color) {
                append_billboard_quad(mesh, position, tangent(radians), up,
                                      width, height, texture_layer, color,
                                      camera_relative_origin);
                append_billboard_quad(mesh, position,
                                      tangent(radians + 1.5707963), up,
                                      width, height, texture_layer, color,
                                      camera_relative_origin);
            };

            if (type_roll < 0.07f) {
                const double height = 0.52 +
                    static_cast<double>(unit_float(splitmix64(key ^ 0x49u))) *
                    0.18;
                billboard(angle, 0.34, height, kFlowerLayer,
                          glm::vec3(0.92f, 0.92f, 0.92f));
            } else if (type_roll < 0.12f) {
                const double height = 0.72 +
                    static_cast<double>(unit_float(splitmix64(key ^ 0x4du))) *
                    0.22;
                billboard(angle, 0.78, height, kShrubLayer,
                          glm::vec3(0.72f, 0.86f, 0.66f));
            } else if (type_roll < 0.30f) {
                const double height = 0.34 +
                    static_cast<double>(unit_float(splitmix64(key ^ 0x51u))) *
                    0.18;
                billboard(angle, 0.62, height, kFernLayer,
                          glm::vec3(0.70f, 0.90f, 0.52f));
            } else {
                const double height = 0.48 +
                    static_cast<double>(unit_float(splitmix64(key ^ 0x37u))) *
                    0.24;
                billboard(angle, 0.30, height, kGrassLayer,
                          glm::vec3(0.68f, 0.84f, 0.28f));
            }
        }
    }

    mesh.content_hash = content_hash(chunk, camera_relative_origin,
                                     lod_level);
    return mesh;
}

} // namespace vegetation
