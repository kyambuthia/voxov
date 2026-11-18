#pragma once

#include "engine_render/render_types.hpp"
#include "engine_world/planet_blocks.hpp"

#include <cstdint>

namespace vegetation {

// Render-only material sentinel. Vegetation is deliberately not a voxel
// material: it has no collision volume and does not increase network state.
inline constexpr uint8_t kGrassMaterial = 250;
// Vegetation is rendered by the alpha-blended billboard pipeline. Keep the
// old name as a source-compatible alias for tests and downstream callers.
inline constexpr uint8_t kBillboardVegetationMaterial = kVegetationRenderMaterial;
inline constexpr uint8_t kVoxelVegetationMaterial = kBillboardVegetationMaterial;
// Layers 0..5 retain legacy crossed-card textures. The runtime-packed atlas
// appends four 8-frame directional impostor sets after them.
inline constexpr uint8_t kGrassLayer = 6;
inline constexpr uint8_t kFlowerLayer = 14;
inline constexpr uint8_t kShrubLayer = 22;
inline constexpr uint8_t kFernLayer = 30;

enum class VegetationStyle : uint8_t {
    MinecraftCrossed = 0,
    VintageStoryWind = 1,
    VoxelCuboids = 2,
    ImpostorCards = 3,
    Off = 4,
    Count = 5,
};

const char *vegetation_style_name(VegetationStyle style);

uint64_t mesh_id(const BlockAddress &chunk);

// Build deterministic vegetation geometry for one streamed surface chunk.
// The returned mesh is empty but still carries a stable ID when the chunk has
// no eligible columns or style == Off, making it a valid residency marker.
RenderMesh build_vegetation_mesh(const BlockWorld &world,
                                 const BlockAddress &chunk,
                                 const VoxelChunk &voxels,
                                 const glm::dvec3 &camera_relative_origin,
                                 VegetationStyle style,
                                 int32_t lod_level = 0);

// Legacy entry point preserved for existing tests/callers (ImpostorCards).
RenderMesh build_grass_mesh(const BlockWorld &world,
                            const BlockAddress &chunk,
                            const VoxelChunk &voxels,
                            const glm::dvec3 &camera_relative_origin,
                            int32_t lod_level = 0);

uint64_t flat_mesh_id(int32_t chunk_x, int32_t chunk_z);

RenderMesh build_flat_vegetation_mesh(const VoxelChunk &voxels,
                                      int32_t chunk_x,
                                      int32_t chunk_z,
                                      const glm::dvec3 &camera_relative_origin,
                                      VegetationStyle style,
                                      uint64_t world_seed);

} // namespace vegetation
