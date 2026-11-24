#include "engine_world/vegetation.hpp"

#include "engine_world/planet.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace vegetation {
namespace {

constexpr uint8_t kLegacyGrassLayerA = 0;
constexpr uint8_t kLegacyGrassLayerB = 1;
constexpr uint8_t kLegacyFoliageLayerA = 2;
constexpr uint8_t kLegacyFoliageLayerB = 3;
constexpr uint8_t kLegacyFlowerLayerA = 4;
constexpr uint8_t kLegacyFlowerLayerB = 5;

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
                           const glm::vec3 &base_color,
                           const glm::vec3 &tip_color,
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
    mesh.vertices.push_back({glm::vec3(corners[0] - camera_origin), base_color,
                             render_normal, {0.0f, 1.0f, layer}});
    mesh.vertices.push_back({glm::vec3(corners[1] - camera_origin), base_color,
                             render_normal, {1.0f, 1.0f, layer}});
    mesh.vertices.push_back({glm::vec3(corners[2] - camera_origin), tip_color,
                             render_normal, {1.0f, 0.0f, layer}});
    mesh.vertices.push_back({glm::vec3(corners[3] - camera_origin), tip_color,
                             render_normal, {0.0f, 0.0f, layer}});
    mesh.indices.insert(mesh.indices.end(),
                        {start, start + 1u, start + 2u,
                         start, start + 2u, start + 3u});
}

void append_voxel_box(RenderMesh &mesh,
                      const glm::dvec3 &base_center,
                      const glm::dvec3 &axis_x,
                      const glm::dvec3 &axis_y,
                      const glm::dvec3 &axis_z,
                      double half_w,
                      double height,
                      double half_d,
                      const glm::vec3 &base_color,
                      const glm::vec3 &tip_color,
                      const glm::dvec3 &camera_origin) {
    const glm::dvec3 dx = axis_x * half_w;
    const glm::dvec3 dy = axis_y * height;
    const glm::dvec3 dz = axis_z * half_d;

    const glm::dvec3 p[8] = {
        base_center - dx - dz,
        base_center + dx - dz,
        base_center + dx + dz,
        base_center - dx + dz,
        base_center - dx - dz + dy,
        base_center + dx - dz + dy,
        base_center + dx + dz + dy,
        base_center - dx + dz + dy,
    };

    const glm::vec3 c[8] = {
        base_color, base_color, base_color, base_color,
        tip_color,  tip_color,  tip_color,  tip_color,
    };

    auto emit_face = [&](int i0, int i1, int i2, int i3, const glm::dvec3 &n) {
        const uint32_t start = static_cast<uint32_t>(mesh.vertices.size());
        const glm::vec3 nf(n);
        // texcoord.z = -0.1f selects pure vertex-colored opaque shading.
        const glm::vec3 tc(0.0f, 0.0f, -0.1f);
        mesh.vertices.push_back({glm::vec3(p[i0] - camera_origin), c[i0], nf, tc});
        mesh.vertices.push_back({glm::vec3(p[i1] - camera_origin), c[i1], nf, tc});
        mesh.vertices.push_back({glm::vec3(p[i2] - camera_origin), c[i2], nf, tc});
        mesh.vertices.push_back({glm::vec3(p[i3] - camera_origin), c[i3], nf, tc});
        mesh.indices.insert(mesh.indices.end(),
                            {start, start + 1u, start + 2u,
                             start, start + 2u, start + 3u});
    };

    emit_face(3, 2, 6, 7, axis_z);   // +Z
    emit_face(1, 0, 4, 5, -axis_z);  // -Z
    emit_face(2, 1, 5, 6, axis_x);   // +X
    emit_face(0, 3, 7, 4, -axis_x);  // -X
    emit_face(7, 6, 5, 4, axis_y);   // +Y (top)
}

uint64_t content_hash(const BlockAddress &chunk,
                      const glm::dvec3 &camera_origin,
                      VegetationStyle style,
                      int32_t lod_level) {
    uint64_t hash = splitmix64(chunk_key(chunk) ^
                               (static_cast<uint64_t>(lod_level) << 56u) ^
                               (static_cast<uint64_t>(style) << 48u));
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

const char *vegetation_style_name(VegetationStyle style) {
    switch (style) {
    case VegetationStyle::MinecraftCrossed: return "Minecraft Crossed Quads";
    case VegetationStyle::VintageStoryWind: return "Vintage Story / Shader Wind";
    case VegetationStyle::VoxelCuboids:     return "Veloren / Teardown 3D Voxels";
    case VegetationStyle::ImpostorCards:    return "Original 8-Frame Impostors";
    case VegetationStyle::Off:              return "Off";
    default:                                return "Unknown";
    }
}

uint64_t mesh_id(const BlockAddress &chunk) {
    // Keep the terrain and vegetation IDs in separate namespaces while
    // retaining the same stable address-to-GPU-cache relationship.
    return BlockWorld::chunk_mesh_id(chunk) ^ 0x5645474554415445ull;
}

RenderMesh build_vegetation_mesh(const BlockWorld &world,
                                 const BlockAddress &chunk,
                                 const VoxelChunk &voxels,
                                 const glm::dvec3 &camera_relative_origin,
                                 VegetationStyle style,
                                 int32_t lod_level) {
    RenderMesh mesh{};
    mesh.mesh_id = mesh_id(chunk);
    mesh.world_origin = camera_relative_origin;
    mesh.material = (style == VegetationStyle::VoxelCuboids)
                        ? 0
                        : kBillboardVegetationMaterial;
    mesh.double_sided = false;

    const int32_t surface_shell = world.shell_count() - 1;
    if (style == VegetationStyle::Off ||
        chunk.shell != surface_shell || voxels.is_empty()) {
        mesh.content_hash = content_hash(chunk, camera_relative_origin,
                                         style, lod_level);
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

    mesh.vertices.reserve(384);
    mesh.indices.reserve(768);
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
            const uint64_t patch_key = splitmix64(
                seed ^ static_cast<uint64_t>(static_cast<uint32_t>(global_x / 4)) *
                    0x9e3779b9u ^
                static_cast<uint64_t>(static_cast<uint32_t>(global_z / 4)) *
                    0x85ebca6bu);
            const float patch = unit_float(patch_key);
            const float density = (style == VegetationStyle::VintageStoryWind)
                                      ? (0.28f + patch * 0.36f)
                                      : (0.20f + patch * 0.30f);
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
            east = glm::normalize(glm::cross(east_hint, up));
            north = glm::normalize(glm::cross(up, east));
            position = planet_center + up *
                (world.surface_radial_distance(up) + 0.005);

            const float type_roll = unit_float(splitmix64(key ^ 0x31u));
            const double angle = static_cast<double>(
                unit_float(splitmix64(key ^ 0x59u)) * 6.2831853f);

            auto tangent = [&](double radians) {
                return east * std::cos(radians) +
                       north * std::sin(radians);
            };

            if (style == VegetationStyle::MinecraftCrossed) {
                auto x_mesh = [&](double radians, double width, double height,
                                  uint8_t layer, const glm::vec3 &color) {
                    const glm::vec3 base_c = color * 0.62f;
                    const glm::vec3 tip_c = color;
                    for (int card = 0; card < 2; ++card) {
                        const double a = radians + 1.5707963 * card;
                        append_billboard_quad(mesh, position, tangent(a), up,
                                              width, height, layer, base_c,
                                              tip_c, camera_relative_origin);
                    }
                };
                if (type_roll < 0.08f) {
                    const uint8_t layer = (unit_float(splitmix64(key ^ 0x61u)) < 0.5f)
                                              ? kLegacyFlowerLayerA
                                              : kLegacyFlowerLayerB;
                    x_mesh(angle, 0.52, 0.55, layer, glm::vec3(0.95f));
                } else if (type_roll < 0.22f) {
                    const uint8_t layer = (unit_float(splitmix64(key ^ 0x63u)) < 0.5f)
                                              ? kLegacyFoliageLayerA
                                              : kLegacyFoliageLayerB;
                    x_mesh(angle, 0.72, 0.68, layer,
                           glm::vec3(0.88f, 0.96f, 0.82f));
                } else {
                    const uint8_t layer = (unit_float(splitmix64(key ^ 0x65u)) < 0.5f)
                                              ? kLegacyGrassLayerA
                                              : kLegacyGrassLayerB;
                    const double h = 0.52 +
                        static_cast<double>(unit_float(splitmix64(key ^ 0x37u))) *
                        0.28;
                    x_mesh(angle, 0.68, h, layer,
                           glm::vec3(0.92f, 0.98f, 0.86f));
                }
            } else if (style == VegetationStyle::VintageStoryWind) {
                auto hash_mesh = [&](double radians, double width, double height,
                                     uint8_t layer, const glm::vec3 &color,
                                     int planes) {
                    const glm::vec3 base_c = color * 0.50f;
                    const glm::vec3 tip_c = color * glm::vec3(1.06f, 1.10f, 0.92f);
                    for (int card = 0; card < planes; ++card) {
                        const double a = radians +
                            3.14159265 * static_cast<double>(card) /
                                static_cast<double>(planes);
                        const glm::dvec3 t = tangent(a);
                        const glm::dvec3 n = glm::normalize(glm::cross(t, up));
                        const glm::dvec3 offset =
                            (planes >= 4) ? n * (width * 0.14 * ((card & 1) ? 1.0 : -1.0))
                                          : glm::dvec3(0.0);
                        append_billboard_quad(mesh, position + offset, t, up,
                                              width, height, layer, base_c,
                                              tip_c, camera_relative_origin);
                    }
                };
                if (type_roll < 0.08f) {
                    const uint8_t layer = (unit_float(splitmix64(key ^ 0x61u)) < 0.5f)
                                              ? kLegacyFlowerLayerA
                                              : kLegacyFlowerLayerB;
                    hash_mesh(angle, 0.48, 0.56, layer, glm::vec3(0.98f), 2);
                } else if (type_roll < 0.24f) {
                    const uint8_t layer = (unit_float(splitmix64(key ^ 0x63u)) < 0.5f)
                                              ? kLegacyFoliageLayerA
                                              : kLegacyFoliageLayerB;
                    hash_mesh(angle, 0.80, 0.74, layer,
                              glm::vec3(0.88f, 0.96f, 0.82f), 3);
                } else {
                    const uint8_t layer = (unit_float(splitmix64(key ^ 0x65u)) < 0.5f)
                                              ? kLegacyGrassLayerA
                                              : kLegacyGrassLayerB;
                    const double h = 0.58 +
                        static_cast<double>(unit_float(splitmix64(key ^ 0x37u))) *
                        0.34;
                    hash_mesh(angle, 0.72, h, layer,
                              glm::vec3(0.92f, 0.98f, 0.86f), 4);
                }
            } else if (style == VegetationStyle::VoxelCuboids) {
                if (type_roll < 0.08f) {
                    // 3D voxel flower: green stem + colored petal cap
                    const glm::vec3 stem_base(0.18f, 0.34f, 0.10f);
                    const glm::vec3 stem_tip(0.28f, 0.52f, 0.16f);
                    const double stem_h = 0.28 +
                        static_cast<double>(unit_float(splitmix64(key ^ 0x49u))) *
                        0.12;
                    append_voxel_box(mesh, position, east, up, north,
                                     0.03, stem_h, 0.03,
                                     stem_base, stem_tip,
                                     camera_relative_origin);
                    const float hue = unit_float(splitmix64(key ^ 0x71u));
                    const glm::vec3 petal =
                        (hue < 0.33f) ? glm::vec3(0.95f, 0.82f, 0.18f) :
                        (hue < 0.66f) ? glm::vec3(0.92f, 0.30f, 0.36f) :
                                        glm::vec3(0.52f, 0.68f, 0.96f);
                    append_voxel_box(mesh, position + up * stem_h,
                                     east, up, north,
                                     0.09, 0.08, 0.09,
                                     petal * 0.85f, petal,
                                     camera_relative_origin);
                } else if (type_roll < 0.18f) {
                    // 3D voxel shrub cluster
                    const glm::vec3 shrub_base(0.20f, 0.34f, 0.10f);
                    const glm::vec3 shrub_tip(0.38f, 0.58f, 0.18f);
                    append_voxel_box(mesh, position, east, up, north,
                                     0.20, 0.30, 0.20,
                                     shrub_base, shrub_tip,
                                     camera_relative_origin);
                    append_voxel_box(mesh, position + east * 0.10 + up * 0.18,
                                     east, up, north,
                                     0.13, 0.20, 0.13,
                                     shrub_base * 1.1f, shrub_tip,
                                     camera_relative_origin);
                } else {
                    // 3D micro-voxel grass blade tuft (3 blades)
                    const glm::vec3 base_c(0.22f, 0.36f, 0.10f);
                    const glm::vec3 tip_c(0.46f, 0.68f, 0.18f);
                    for (int b = 0; b < 3; ++b) {
                        const double ba = angle + 2.0943951 * b;
                        const glm::dvec3 bx = tangent(ba);
                        const glm::dvec3 bz = glm::normalize(glm::cross(bx, up));
                        const glm::dvec3 bpos = position + bx * 0.08;
                        const double bh = 0.24 +
                            static_cast<double>(unit_float(
                                splitmix64(key ^ (0x81u + b)))) * 0.22;
                        append_voxel_box(mesh, bpos, bx, up, bz,
                                         0.032, bh, 0.032,
                                         base_c, tip_c,
                                         camera_relative_origin);
                    }
                }
            } else {
                // VegetationStyle::ImpostorCards (original 8-frame impostors)
                auto billboard = [&](double radians, double width,
                                     double height, uint8_t texture_layer,
                                     const glm::vec3 &color, int card_count) {
                    for (int card = 0; card < card_count; ++card) {
                        const double card_angle = radians +
                            3.14159265 * static_cast<double>(card) /
                                static_cast<double>(card_count);
                        append_billboard_quad(mesh, position, tangent(card_angle),
                                              up, width, height, texture_layer,
                                              color, color,
                                              camera_relative_origin);
                    }
                };
                if (type_roll < 0.07f) {
                    const double height = 0.52 +
                        static_cast<double>(unit_float(splitmix64(key ^ 0x49u))) *
                        0.18;
                    billboard(angle, 0.34, height, kFlowerLayer,
                              glm::vec3(0.92f, 0.92f, 0.92f), 2);
                } else if (type_roll < 0.12f) {
                    const double height = 0.86 +
                        static_cast<double>(unit_float(splitmix64(key ^ 0x4du))) *
                        0.28;
                    billboard(angle, 1.08, height, kShrubLayer,
                              glm::vec3(0.72f, 0.86f, 0.66f), 3);
                } else if (type_roll < 0.30f) {
                    const double height = 0.34 +
                        static_cast<double>(unit_float(splitmix64(key ^ 0x51u))) *
                        0.18;
                    billboard(angle, 0.62, height, kFernLayer,
                              glm::vec3(0.70f, 0.90f, 0.52f), 2);
                } else {
                    const double height = 0.60 +
                        static_cast<double>(unit_float(splitmix64(key ^ 0x37u))) *
                        0.28;
                    billboard(angle, 0.42, height, kGrassLayer,
                              glm::vec3(0.68f, 0.84f, 0.28f), 3);
                }
            }
        }
    }

    mesh.content_hash = content_hash(chunk, camera_relative_origin,
                                     style, lod_level);
    return mesh;
}

RenderMesh build_grass_mesh(const BlockWorld &world,
                            const BlockAddress &chunk,
                            const VoxelChunk &voxels,
                            const glm::dvec3 &camera_relative_origin,
                            int32_t lod_level) {
    return build_vegetation_mesh(world, chunk, voxels, camera_relative_origin,
                                 VegetationStyle::ImpostorCards, lod_level);
}


uint64_t flat_mesh_id(int32_t chunk_x, int32_t chunk_z) {
    uint64_t key = static_cast<uint64_t>(static_cast<uint32_t>(chunk_x)) << 32u;
    key ^= static_cast<uint64_t>(static_cast<uint32_t>(chunk_z));
    uint64_t id = splitmix64(key ^ 0x464C4154564547ull);
    return id == 0 ? 1 : id;
}

RenderMesh build_flat_vegetation_mesh(const VoxelChunk &voxels,
                                      int32_t chunk_x,
                                      int32_t chunk_z,
                                      const glm::dvec3 &camera_relative_origin,
                                      VegetationStyle style,
                                      uint64_t world_seed) {
    RenderMesh mesh{};
    mesh.mesh_id = flat_mesh_id(chunk_x, chunk_z);
    mesh.world_origin = camera_relative_origin;
    mesh.material = (style == VegetationStyle::VoxelCuboids)
                        ? 0
                        : kBillboardVegetationMaterial;
    mesh.double_sided = false;

    if (style == VegetationStyle::Off || voxels.is_empty()) {
        return mesh;
    }

    const int sx = voxels.size_x();
    const int sy = voxels.size_y();
    const int sz = voxels.size_z();
    const int32_t base_x = chunk_x * sx;
    const int32_t base_z = chunk_z * sz;
    const uint64_t seed = world_seed ^ 0x4752415353ull;
    const glm::dvec3 up(0.0, 1.0, 0.0);
    const glm::dvec3 east(1.0, 0.0, 0.0);
    const glm::dvec3 north(0.0, 0.0, 1.0);

    mesh.vertices.reserve(512);
    mesh.indices.reserve(1024);

    for (int z = 0; z < sz; ++z) {
        for (int x = 0; x < sx; ++x) {
            int top_y = -1;
            for (int y = sy - 1; y >= 0; --y) {
                if (voxels.solid(x, y, z)) {
                    top_y = y;
                    break;
                }
            }
            if (top_y < 0 || voxels.material(x, top_y, z) != VoxelMaterial::Grass) {
                continue;
            }
            const int32_t gx = base_x + x;
            const int32_t gz = base_z + z;
            const uint64_t key = splitmix64(
                seed ^
                (static_cast<uint64_t>(static_cast<uint32_t>(gx)) << 17u) ^
                static_cast<uint64_t>(static_cast<uint32_t>(gz)));
            const uint64_t patch_key = splitmix64(
                seed ^ static_cast<uint64_t>(static_cast<uint32_t>(gx / 4)) * 0x9e3779b9u ^
                static_cast<uint64_t>(static_cast<uint32_t>(gz / 4)) * 0x85ebca6bu);
            const float patch = unit_float(patch_key);
            const float density = (style == VegetationStyle::VintageStoryWind)
                                      ? (0.28f + patch * 0.36f)
                                      : (0.20f + patch * 0.30f);
            if (unit_float(key) > density) continue;

            const float jx = unit_float(splitmix64(key ^ 0x11u)) - 0.5f;
            const float jz = unit_float(splitmix64(key ^ 0x29u)) - 0.5f;
            const glm::dvec3 position(
                static_cast<double>(gx) + 0.5 + static_cast<double>(jx) * 0.32,
                static_cast<double>(top_y + 1) + 0.005,
                static_cast<double>(gz) + 0.5 + static_cast<double>(jz) * 0.32);

            const float type_roll = unit_float(splitmix64(key ^ 0x31u));
            const double angle = static_cast<double>(
                unit_float(splitmix64(key ^ 0x59u)) * 6.2831853f);
            auto tangent = [&](double radians) {
                return east * std::cos(radians) + north * std::sin(radians);
            };

            const float tree_roll = unit_float(splitmix64(key ^ 0x99u));
            if (x >= 2 && x < sx - 2 && z >= 2 && z < sz - 2 &&
                tree_roll < 0.016f) {
                const glm::vec3 bark_dark(0.24f, 0.15f, 0.09f);
                const glm::vec3 bark_light(0.36f, 0.24f, 0.14f);
                const bool is_pine = unit_float(splitmix64(key ^ 0xA3u)) < 0.42f;
                const double scale =
                    0.85 + static_cast<double>(unit_float(splitmix64(key ^ 0xA5u))) * 0.45;
                if (is_pine) {
                    const double trunk_h = 4.6 * scale;
                    append_voxel_box(mesh, position, east, up, north,
                                     0.16 * scale, trunk_h * 0.55, 0.16 * scale,
                                     bark_dark, bark_light, camera_relative_origin);
                    append_voxel_box(mesh, position + up * (trunk_h * 0.52), east, up, north,
                                     0.11 * scale, trunk_h * 0.45, 0.11 * scale,
                                     bark_dark, bark_light, camera_relative_origin);
                    const glm::vec3 pine_dark(0.09f, 0.24f, 0.13f);
                    const glm::vec3 pine_tip(0.18f, 0.42f, 0.22f);
                    for (int tier = 0; tier < 5; ++tier) {
                        const double ty = (1.35 + 0.68 * static_cast<double>(tier)) * scale;
                        const double tr = (0.92 - 0.16 * static_cast<double>(tier)) * scale;
                        const double th = (0.48 - 0.05 * static_cast<double>(tier)) * scale;
                        append_voxel_box(mesh, position + up * ty, east, up, north,
                                         tr, th, tr, pine_dark, pine_tip, camera_relative_origin);
                        append_voxel_box(mesh, position + up * (ty + th * 0.65), east, up, north,
                                         tr * 0.68, th * 0.72, tr * 0.68,
                                         pine_dark * 1.1f, pine_tip, camera_relative_origin);
                    }
                    append_voxel_box(mesh, position + up * (trunk_h + 0.12 * scale), east, up, north,
                                     0.14 * scale, 0.36 * scale, 0.14 * scale,
                                     pine_tip * 0.9f, pine_tip, camera_relative_origin);
                } else {
                    const double trunk_h = 3.2 * scale;
                    for (int r = 0; r < 4; ++r) {
                        const double ra = angle + 1.5707963 * r;
                        const glm::dvec3 rdir = tangent(ra);
                        append_voxel_box(mesh, position + rdir * (0.20 * scale), east, up, north,
                                         0.12 * scale, 0.26 * scale, 0.12 * scale,
                                         bark_dark, bark_light, camera_relative_origin);
                    }
                    append_voxel_box(mesh, position, east, up, north,
                                     0.20 * scale, trunk_h * 0.55, 0.20 * scale,
                                     bark_dark, bark_light, camera_relative_origin);
                    append_voxel_box(mesh, position + up * (trunk_h * 0.52), east, up, north,
                                     0.15 * scale, trunk_h * 0.50, 0.15 * scale,
                                     bark_dark, bark_light, camera_relative_origin);

                    const glm::vec3 leaf_dark(0.14f, 0.34f, 0.11f);
                    const glm::vec3 leaf_bright(0.32f, 0.60f, 0.18f);
                    for (int b = 0; b < 4; ++b) {
                        const double ba = angle + 1.5707963 * b + 0.35;
                        const glm::dvec3 bdir = tangent(ba);
                        const double by = (2.15 + 0.22 * (b % 2)) * scale;
                        append_voxel_box(mesh, position + up * by + bdir * (0.42 * scale),
                                         east, up, north,
                                         0.11 * scale, 0.11 * scale, 0.42 * scale,
                                         bark_dark, bark_light, camera_relative_origin);
                        const glm::dvec3 pod_pos = position + up * (by + 0.12 * scale) + bdir * (0.78 * scale);
                        append_voxel_box(mesh, pod_pos, east, up, north,
                                         0.52 * scale, 0.48 * scale, 0.52 * scale,
                                         leaf_dark, leaf_bright, camera_relative_origin);
                        append_voxel_box(mesh, pod_pos + up * (0.42 * scale) + bdir * (0.14 * scale),
                                         east, up, north,
                                         0.28 * scale, 0.24 * scale, 0.28 * scale,
                                         leaf_dark * 1.15f, leaf_bright, camera_relative_origin);
                    }
                    append_voxel_box(mesh, position + up * (trunk_h - 0.1 * scale),
                                     east, up, north,
                                     0.74 * scale, 0.56 * scale, 0.74 * scale,
                                     leaf_dark, leaf_bright, camera_relative_origin);
                    append_voxel_box(mesh, position + up * (trunk_h + 0.42 * scale),
                                     east, up, north,
                                     0.46 * scale, 0.38 * scale, 0.46 * scale,
                                     leaf_dark * 1.1f, leaf_bright, camera_relative_origin);
                    append_voxel_box(mesh, position + up * (trunk_h + 0.76 * scale) + east * (0.12 * scale),
                                     east, up, north,
                                     0.22 * scale, 0.20 * scale, 0.22 * scale,
                                     leaf_bright * 0.85f, leaf_bright, camera_relative_origin);
                }
                continue;
            }
            if (style == VegetationStyle::MinecraftCrossed) {
                auto x_mesh = [&](double radians, double width, double height,
                                  uint8_t layer, const glm::vec3 &color) {
                    const glm::vec3 base_c = color * 0.62f;
                    for (int card = 0; card < 2; ++card) {
                        const double a = radians + 1.5707963 * card;
                        append_billboard_quad(mesh, position, tangent(a), up,
                                              width, height, layer, base_c,
                                              color, camera_relative_origin);
                    }
                };
                if (type_roll < 0.08f) {
                    const uint8_t layer = (unit_float(splitmix64(key ^ 0x61u)) < 0.5f)
                                              ? kLegacyFlowerLayerA : kLegacyFlowerLayerB;
                    x_mesh(angle, 0.52, 0.55, layer, glm::vec3(0.95f));
                } else if (type_roll < 0.22f) {
                    const uint8_t layer = (unit_float(splitmix64(key ^ 0x63u)) < 0.5f)
                                              ? kLegacyFoliageLayerA : kLegacyFoliageLayerB;
                    x_mesh(angle, 0.72, 0.68, layer, glm::vec3(0.88f, 0.96f, 0.82f));
                } else {
                    const uint8_t layer = (unit_float(splitmix64(key ^ 0x65u)) < 0.5f)
                                              ? kLegacyGrassLayerA : kLegacyGrassLayerB;
                    const double h = 0.52 + static_cast<double>(unit_float(splitmix64(key ^ 0x37u))) * 0.28;
                    x_mesh(angle, 0.68, h, layer, glm::vec3(0.92f, 0.98f, 0.86f));
                }
            } else if (style == VegetationStyle::VintageStoryWind) {
                auto hash_mesh = [&](double radians, double width, double height,
                                     uint8_t layer, const glm::vec3 &color, int planes) {
                    const glm::vec3 base_c = color * 0.50f;
                    const glm::vec3 tip_c = color * glm::vec3(1.06f, 1.10f, 0.92f);
                    for (int card = 0; card < planes; ++card) {
                        const double a = radians + 3.14159265 * static_cast<double>(card) / static_cast<double>(planes);
                        const glm::dvec3 t = tangent(a);
                        const glm::dvec3 n = glm::normalize(glm::cross(t, up));
                        const glm::dvec3 offset = (planes >= 4) ? n * (width * 0.14 * ((card & 1) ? 1.0 : -1.0)) : glm::dvec3(0.0);
                        append_billboard_quad(mesh, position + offset, t, up,
                                              width, height, layer, base_c,
                                              tip_c, camera_relative_origin);
                    }
                };
                if (type_roll < 0.08f) {
                    const uint8_t layer = (unit_float(splitmix64(key ^ 0x61u)) < 0.5f)
                                              ? kLegacyFlowerLayerA : kLegacyFlowerLayerB;
                    hash_mesh(angle, 0.48, 0.56, layer, glm::vec3(0.98f), 2);
                } else if (type_roll < 0.24f) {
                    const uint8_t layer = (unit_float(splitmix64(key ^ 0x63u)) < 0.5f)
                                              ? kLegacyFoliageLayerA : kLegacyFoliageLayerB;
                    hash_mesh(angle, 0.80, 0.74, layer, glm::vec3(0.88f, 0.96f, 0.82f), 3);
                } else {
                    const uint8_t layer = (unit_float(splitmix64(key ^ 0x65u)) < 0.5f)
                                              ? kLegacyGrassLayerA : kLegacyGrassLayerB;
                    const double h = 0.58 + static_cast<double>(unit_float(splitmix64(key ^ 0x37u))) * 0.34;
                    hash_mesh(angle, 0.72, h, layer, glm::vec3(0.92f, 0.98f, 0.86f), 4);
                }
            } else if (style == VegetationStyle::VoxelCuboids) {
                if (type_roll < 0.08f) {
                    const double stem_h = 0.28 + static_cast<double>(unit_float(splitmix64(key ^ 0x49u))) * 0.12;
                    append_voxel_box(mesh, position, east, up, north, 0.03, stem_h, 0.03,
                                     glm::vec3(0.18f, 0.34f, 0.10f), glm::vec3(0.28f, 0.52f, 0.16f), camera_relative_origin);
                    const float hue = unit_float(splitmix64(key ^ 0x71u));
                    const glm::vec3 petal = (hue < 0.33f) ? glm::vec3(0.95f, 0.82f, 0.18f)
                                          : (hue < 0.66f) ? glm::vec3(0.92f, 0.30f, 0.36f)
                                                          : glm::vec3(0.52f, 0.68f, 0.96f);
                    append_voxel_box(mesh, position + up * stem_h, east, up, north, 0.09, 0.08, 0.09,
                                     petal * 0.85f, petal, camera_relative_origin);
                } else if (type_roll < 0.18f) {
                    append_voxel_box(mesh, position, east, up, north, 0.20, 0.30, 0.20,
                                     glm::vec3(0.20f, 0.34f, 0.10f), glm::vec3(0.38f, 0.58f, 0.18f), camera_relative_origin);
                    append_voxel_box(mesh, position + east * 0.10 + up * 0.18, east, up, north, 0.13, 0.20, 0.13,
                                     glm::vec3(0.22f, 0.37f, 0.11f), glm::vec3(0.38f, 0.58f, 0.18f), camera_relative_origin);
                } else {
                    for (int b = 0; b < 3; ++b) {
                        const double ba = angle + 2.0943951 * b;
                        const glm::dvec3 bx = tangent(ba);
                        const glm::dvec3 bz = glm::normalize(glm::cross(bx, up));
                        const double bh = 0.24 + static_cast<double>(unit_float(splitmix64(key ^ (0x81u + b)))) * 0.22;
                        append_voxel_box(mesh, position + bx * 0.08, bx, up, bz, 0.032, bh, 0.032,
                                         glm::vec3(0.22f, 0.36f, 0.10f), glm::vec3(0.46f, 0.68f, 0.18f), camera_relative_origin);
                    }
                }
            } else {
                auto billboard = [&](double radians, double width, double height,
                                     uint8_t texture_layer, const glm::vec3 &color, int card_count) {
                    for (int card = 0; card < card_count; ++card) {
                        const double card_angle = radians + 3.14159265 * static_cast<double>(card) / static_cast<double>(card_count);
                        append_billboard_quad(mesh, position, tangent(card_angle), up,
                                              width, height, texture_layer, color, color, camera_relative_origin);
                    }
                };
                if (type_roll < 0.07f) {
                    billboard(angle, 0.34, 0.58, kFlowerLayer, glm::vec3(0.92f), 2);
                } else if (type_roll < 0.12f) {
                    billboard(angle, 1.08, 0.96, kShrubLayer, glm::vec3(0.72f, 0.86f, 0.66f), 3);
                } else if (type_roll < 0.30f) {
                    billboard(angle, 0.62, 0.42, kFernLayer, glm::vec3(0.70f, 0.90f, 0.52f), 2);
                } else {
                    billboard(angle, 0.42, 0.72, kGrassLayer, glm::vec3(0.68f, 0.84f, 0.28f), 3);
                }
            }
        }
    }
    return mesh;
}

} // namespace vegetation
