#pragma once

#include "engine_world/voxel_chunk.hpp"

#include <cstdint>
#include <unordered_map>
#include <vector>

#include <glm/vec3.hpp>

struct VoxelChunkCoord {
    int32_t x = 0;
    int32_t z = 0;

    bool operator==(const VoxelChunkCoord &other) const {
        return x == other.x && z == other.z;
    }
};

struct VoxelStaticShape {
    glm::vec3 min = glm::vec3(0.0f);
    glm::vec3 max = glm::vec3(0.0f);
    VoxelChunkCoord chunk{};
};

class VoxelPhysicsBridge {
public:
    void register_chunk(VoxelChunkCoord coord, const VoxelChunk &chunk);
    void unregister_chunk(VoxelChunkCoord coord);
    void clear();

    bool has_chunk(VoxelChunkCoord coord) const;
    bool take_chunk_dirty(VoxelChunkCoord coord);
    size_t chunk_count() const;

    std::vector<VoxelStaticShape> build_chunk_shapes(VoxelChunkCoord coord) const;
    std::vector<VoxelStaticShape> build_all_shapes() const;

private:
    struct ChunkEntry {
        VoxelChunk chunk{};
        bool dirty = true;
    };

    struct ChunkCoordHash {
        size_t operator()(const VoxelChunkCoord &coord) const;
    };

    std::unordered_map<VoxelChunkCoord, ChunkEntry, ChunkCoordHash> chunks;
};
