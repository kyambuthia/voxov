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

uint64_t mesh_id(const BlockAddress &chunk);

// Build deterministic grass billboards for one streamed surface chunk. The
// returned mesh is empty but still carries a stable ID when the chunk has no
// eligible columns, which makes it a valid residency marker for streaming.
RenderMesh build_grass_mesh(const BlockWorld &world,
                            const BlockAddress &chunk,
                            const VoxelChunk &voxels,
                            const glm::dvec3 &camera_relative_origin,
                            int32_t lod_level = 0);

} // namespace vegetation
