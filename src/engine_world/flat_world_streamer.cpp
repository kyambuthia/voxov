#include "engine_world/flat_world_streamer.hpp"
#include "engine_world/voxel_chunk.hpp"

#include <algorithm>
#include <cmath>

void FlatWorldStreamer::init(uint64_t world_seed,
                             const FlatStreamerConfig &config) {
    world_seed_ = world_seed;
    config_ = config;
    chunks_.clear();
    visible_meshes_.clear();
    frame_index_ = 0;
    mesh_set_revision_ = 0;
}

void FlatWorldStreamer::update(const glm::vec3 &camera_pos) {
    ++frame_index_;
    bool mesh_set_changed = false;

    const FlatChunkCoord center = world_to_chunk(camera_pos);
    const int32_t r = config_.view_radius_chunks;

    // Collect desired chunks in view radius
    std::vector<FlatChunkCoord> desired;
    desired.reserve(static_cast<size_t>((2 * r + 1) * (2 * r + 1)));
    for (int32_t dz = -r; dz <= r; ++dz) {
        for (int32_t dx = -r; dx <= r; ++dx) {
            desired.push_back({center.x + dx, center.z + dz});
        }
    }

    // Sort by distance from center (closest first) for budget prioritization
    std::sort(desired.begin(), desired.end(),
              [&](const FlatChunkCoord &a, const FlatChunkCoord &b) {
                  const int da = (a.x - center.x) * (a.x - center.x) +
                                 (a.z - center.z) * (a.z - center.z);
                  const int db = (b.x - center.x) * (b.x - center.x) +
                                 (b.z - center.z) * (b.z - center.z);
                  return da < db;
              });

    // Generate nearby chunks gradually. Visibility culling belongs at draw
    // time; skipping off-camera residency leaves terrain holes on camera turns.
    uint32_t generated = 0;
    for (const FlatChunkCoord &coord : desired) {
        const auto it = chunks_.find(coord);
        if (it != chunks_.end()) {
            it->second.last_requested_frame = frame_index_;
            continue;
        }
        if (generated >= config_.generation_budget_per_update) {
            continue;
        }
        ensure_chunk(coord);
        ++generated;
        mesh_set_changed = true;
    }

    // Evict chunks outside view radius
    std::vector<FlatChunkCoord> to_evict;
    for (auto &[coord, chunk] : chunks_) {
        if (chunk.last_requested_frame != frame_index_) {
            to_evict.push_back(coord);
        }
    }
    for (const FlatChunkCoord &coord : to_evict) {
        chunks_.erase(coord);
        mesh_set_changed = true;
    }

    if (mesh_set_changed) {
        rebuild_visible_meshes();
    }
}

void FlatWorldStreamer::rebuild_visible_meshes() {
    visible_meshes_.clear();
    visible_meshes_.reserve(chunks_.size());
    for (const auto &[coord, chunk] : chunks_) {
        visible_meshes_.push_back(chunk.mesh);
    }
    ++mesh_set_revision_;
}

const std::vector<RenderMesh> &FlatWorldStreamer::render_meshes() const {
    return visible_meshes_;
}

size_t FlatWorldStreamer::streamed_chunk_count() const {
    return chunks_.size();
}

uint64_t FlatWorldStreamer::mesh_set_revision() const {
    return mesh_set_revision_;
}

const FlatStreamerConfig &FlatWorldStreamer::config() const {
    return config_;
}

void FlatWorldStreamer::ensure_chunk(FlatChunkCoord coord) {
    FlatResidentChunk resident{};
    resident.coord = coord;
    resident.last_requested_frame = frame_index_;

    generate_flat_world_locomotion_chunk(resident.voxels, world_seed_, coord.x, coord.z);
    const glm::vec3 origin(
        static_cast<float>(coord.x * VoxelChunk::CHUNK_X), 0.0f,
        static_cast<float>(coord.z * VoxelChunk::CHUNK_Z));
    const int32_t world_base_x = coord.x * VoxelChunk::CHUNK_X;
    const int32_t world_base_z = coord.z * VoxelChunk::CHUNK_Z;
    resident.mesh = resident.voxels.build_greedy_mesh(
        origin, 1.0f, world_base_x, world_base_z,
        [this](int wx, int wy, int wz) {
          return is_solid_at_world(wx, wy, wz);
        });
    resident.mesh.mesh_id = chunk_mesh_id(coord.x, coord.z);
    resident.resident = true;
    chunks_[coord] = std::move(resident);
}

FlatChunkCoord FlatWorldStreamer::world_to_chunk(const glm::vec3 &pos) const {
    const int32_t cx = static_cast<int32_t>(
        std::floor(pos.x / static_cast<float>(VoxelChunk::CHUNK_X)));
    const int32_t cz = static_cast<int32_t>(
        std::floor(pos.z / static_cast<float>(VoxelChunk::CHUNK_Z)));
    return {cx, cz};
}

float FlatWorldStreamer::ground_height_at(float world_x,
                                          float world_z) const {
    WorldGenerator gen(world_seed_);
    return gen.sample_height(world_x, world_z);
}

uint64_t FlatWorldStreamer::chunk_mesh_id(int32_t cx, int32_t cz) {
    // Stable id from chunk coordinates — never collides with 0 (transient).
    uint64_t id = 0x464c4154574f524cull; // "FLATWORL"
    id ^= static_cast<uint64_t>(static_cast<uint32_t>(cx)) << 20u;
    id ^= static_cast<uint64_t>(static_cast<uint32_t>(cz));
    id ^= id >> 27u;
    id *= 0x94d049bb133111ebull;
    id ^= id >> 31u;
    return id | 0x8000000000000000ull; // ensure non-zero high bit
}

bool FlatWorldStreamer::is_solid_at_world(int wx, int wy, int wz) const {
    const int32_t cx = static_cast<int32_t>(
        std::floor(static_cast<float>(wx) /
                   static_cast<float>(VoxelChunk::CHUNK_X)));
    const int32_t cz = static_cast<int32_t>(
        std::floor(static_cast<float>(wz) /
                   static_cast<float>(VoxelChunk::CHUNK_Z)));
    auto it = chunks_.find({cx, cz});
    if (it == chunks_.end() || !it->second.resident) return false;
    const int lx = wx - cx * VoxelChunk::CHUNK_X;
    const int lz = wz - cz * VoxelChunk::CHUNK_Z;
    return it->second.voxels.solid(lx, wy, lz);
}

std::vector<VoxelCollisionChunk>
FlatWorldStreamer::resident_collision_chunks() const {
    std::vector<VoxelCollisionChunk> result;
    result.reserve(chunks_.size());
    for (const auto &[coord, resident] : chunks_) {
        if (!resident.resident) continue;
        VoxelCollisionChunk cc{};
        cc.chunk = &resident.voxels;
        cc.origin_x = coord.x * VoxelChunk::CHUNK_X;
        cc.origin_z = coord.z * VoxelChunk::CHUNK_Z;
        result.push_back(cc);
    }
    return result;
}
