#pragma once

#include "engine_render/render_types.hpp"
#include "engine_world/voxel_chunk.hpp"
#include "engine_world/physics/voxel_collision.hpp"
#include "engine_world/world_gen.hpp"

#include <cstdint>
#include <unordered_map>
#include <vector>

struct FlatChunkCoord {
    int32_t x = 0;
    int32_t z = 0;

    friend bool operator==(const FlatChunkCoord &a,
                           const FlatChunkCoord &b) = default;
};

struct FlatChunkCoordHash {
    size_t operator()(const FlatChunkCoord &coord) const noexcept {
        uint64_t h = static_cast<uint64_t>(static_cast<uint32_t>(coord.x));
        h ^= static_cast<uint64_t>(static_cast<uint32_t>(coord.z)) << 32u;
        h ^= h >> 30u;
        h *= 0xbf58476d1ce4e5b9ull;
        h ^= h >> 27u;
        h *= 0x94d049bb133111ebull;
        h ^= h >> 31u;
        return static_cast<size_t>(h);
    }
};

struct FlatResidentChunk {
    FlatChunkCoord coord{};
    VoxelChunk voxels{};
    RenderMesh mesh{};
    bool resident = false;
    uint64_t last_requested_frame = 0;
};

struct FlatStreamerConfig {
    uint32_t generation_budget_per_update = 4;
    int32_t view_radius_chunks = 3;
};

class FlatWorldStreamer {
public:
    FlatWorldStreamer() = default;

    void init(uint64_t world_seed,
              const FlatStreamerConfig &config = {});
    void update(const glm::vec3 &camera_pos);
    void set_view_projection(const glm::mat4 &vp) { view_projection_ = vp; }
    const std::vector<RenderMesh> &render_meshes() const;
    size_t streamed_chunk_count() const;
    const FlatStreamerConfig &config() const;

    float ground_height_at(float world_x, float world_z) const;
    static uint64_t chunk_mesh_id(int32_t cx, int32_t cz);

    // Returns true if any resident chunk has a solid voxel at (wx,wy,wz).
    bool is_solid_at_world(int wx, int wy, int wz) const;

    // Returns resident chunks within view radius for collision.
    std::vector<VoxelCollisionChunk>
    resident_collision_chunks() const;

private:
    void ensure_chunk(FlatChunkCoord coord);
    FlatChunkCoord world_to_chunk(const glm::vec3 &pos) const;
    bool chunk_visible_in_frustum(FlatChunkCoord coord) const;

    uint64_t world_seed_ = 0;
    FlatStreamerConfig config_{};
    std::unordered_map<FlatChunkCoord, FlatResidentChunk, FlatChunkCoordHash>
        chunks_{};
    std::vector<RenderMesh> visible_meshes_{};
    uint64_t frame_index_ = 0;
    glm::mat4 view_projection_{1.0f};
};
