#include "engine_physics/voxel/voxel_physics_bridge.hpp"

#include <algorithm>
#include <utility>

namespace {
constexpr float k_voxel_size = 1.0f;
}

size_t VoxelPhysicsBridge::ChunkCoordHash::operator()(const VoxelChunkCoord &coord) const {
    const uint64_t x = static_cast<uint64_t>(static_cast<uint32_t>(coord.x));
    const uint64_t z = static_cast<uint64_t>(static_cast<uint32_t>(coord.z));
    return static_cast<size_t>((x << 32u) ^ z);
}

void VoxelPhysicsBridge::register_chunk(VoxelChunkCoord coord, const VoxelChunk &chunk) {
    ChunkEntry &entry = chunks[coord];
    entry.chunk = chunk;
    entry.dirty = true;
}

void VoxelPhysicsBridge::unregister_chunk(VoxelChunkCoord coord) {
    chunks.erase(coord);
}

void VoxelPhysicsBridge::clear() {
    chunks.clear();
}

bool VoxelPhysicsBridge::has_chunk(VoxelChunkCoord coord) const {
    return chunks.find(coord) != chunks.end();
}

bool VoxelPhysicsBridge::take_chunk_dirty(VoxelChunkCoord coord) {
    auto it = chunks.find(coord);
    if (it == chunks.end()) {
        return false;
    }
    const bool was_dirty = it->second.dirty;
    it->second.dirty = false;
    return was_dirty;
}

size_t VoxelPhysicsBridge::chunk_count() const {
    return chunks.size();
}

std::vector<VoxelStaticShape> VoxelPhysicsBridge::build_chunk_shapes(VoxelChunkCoord coord) const {
    std::vector<VoxelStaticShape> shapes;
    auto it = chunks.find(coord);
    if (it == chunks.end()) {
        return shapes;
    }

    const VoxelChunk &chunk = it->second.chunk;
    std::vector<bool> visited(static_cast<size_t>(VoxelChunk::CHUNK_X * VoxelChunk::CHUNK_Y * VoxelChunk::CHUNK_Z), false);

    auto voxel_index = [](int x, int y, int z) -> size_t {
        return static_cast<size_t>((y * VoxelChunk::CHUNK_Z + z) * VoxelChunk::CHUNK_X + x);
    };

    const float chunk_base_x = static_cast<float>(coord.x * VoxelChunk::CHUNK_X);
    const float chunk_base_z = static_cast<float>(coord.z * VoxelChunk::CHUNK_Z);

    for (int y = 0; y < VoxelChunk::CHUNK_Y; ++y) {
        for (int z = 0; z < VoxelChunk::CHUNK_Z; ++z) {
            for (int x = 0; x < VoxelChunk::CHUNK_X; ++x) {
                const size_t idx = voxel_index(x, y, z);
                if (visited[idx] || !chunk.solid(x, y, z)) {
                    continue;
                }

                int run_end_x = x;
                while ((run_end_x + 1) < VoxelChunk::CHUNK_X) {
                    const size_t next_idx = voxel_index(run_end_x + 1, y, z);
                    if (visited[next_idx] || !chunk.solid(run_end_x + 1, y, z)) {
                        break;
                    }
                    ++run_end_x;
                }

                for (int fill_x = x; fill_x <= run_end_x; ++fill_x) {
                    visited[voxel_index(fill_x, y, z)] = true;
                }

                VoxelStaticShape shape{};
                shape.chunk = coord;
                shape.min = glm::vec3(
                    chunk_base_x + static_cast<float>(x) * k_voxel_size,
                    static_cast<float>(y) * k_voxel_size,
                    chunk_base_z + static_cast<float>(z) * k_voxel_size);
                shape.max = glm::vec3(
                    chunk_base_x + static_cast<float>(run_end_x + 1) * k_voxel_size,
                    static_cast<float>(y + 1) * k_voxel_size,
                    chunk_base_z + static_cast<float>(z + 1) * k_voxel_size);
                shapes.push_back(shape);
            }
        }
    }
    return shapes;
}

std::vector<VoxelStaticShape> VoxelPhysicsBridge::build_all_shapes() const {
    std::vector<VoxelStaticShape> all;
    for (const auto &[coord, _] : chunks) {
        const std::vector<VoxelStaticShape> chunk_shapes = build_chunk_shapes(coord);
        all.insert(all.end(), chunk_shapes.begin(), chunk_shapes.end());
    }
    return all;
}
