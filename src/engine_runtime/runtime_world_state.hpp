#pragma once

#include "engine_net_proto/net_types.hpp"
#include "engine_render/render_types.hpp"
#include "engine_world/physics/voxel_collision.hpp"
#include "engine_world/voxel_chunk.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct RuntimeStreamedChunk {
    NetChunkState state{};
};

class RuntimeWorldState {
public:
    void initialize(
        VoxelChunk &world_chunk,
        VoxelCollisionWorld &collision_world,
        RenderScene &scene);
    void reset_streamed_chunks();
    void rebuild_streamed_chunk_scene(
        const VoxelChunk &world_chunk,
        RenderScene &scene);
    std::unordered_map<int32_t, RuntimeStreamedChunk> streamed_chunks;

private:
    mutable uint64_t next_mesh_id_ = 1;
    mutable std::unordered_map<int32_t, RenderMesh> chunk_mesh_cache_;
    std::unordered_map<int32_t, VoxelChunk> collision_chunks_;
};
