#include "engine_runtime/runtime_world_state.hpp"

#include "engine_net/net_runtime_shared.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace {
constexpr int k_world_chunk_radius = 1;

int32_t render_chunk_key(NetChunkCoord coord) {
    return (static_cast<int32_t>(coord.x) << 16) ^
        static_cast<uint16_t>(coord.z);
}
} // namespace

void RuntimeWorldState::initialize(
    VoxelChunk &world_chunk,
    VoxelCollisionWorld &collision_world,
    RenderScene &scene) {
    world_chunk.generate_spherical_planet_seeded(k_voxov_flat_world_seed);

    scene = RenderScene{};
    reset_streamed_chunks();
    collision_chunks_.clear();
    collision_chunks_.reserve(streamed_chunks.size());

    std::vector<VoxelCollisionChunk> collision_chunks;
    collision_chunks.reserve(streamed_chunks.size());
    for (const auto &[key, streamed_chunk] : streamed_chunks) {
        (void)key;
        VoxelChunk chunk{};
        net_generate_chunk_from_state(chunk, streamed_chunk.state);

        const NetChunkCoord coord = streamed_chunk.state.coord;
        const int32_t chunk_key = render_chunk_key(coord);
        auto [it, inserted] = collision_chunks_.emplace(chunk_key, std::move(chunk));
        (void)inserted;
        collision_chunks.push_back(VoxelCollisionChunk{
            &it->second,
            static_cast<int32_t>(coord.x) * VoxelChunk::CHUNK_X,
            static_cast<int32_t>(coord.z) * VoxelChunk::CHUNK_Z,
        });

        if (coord.x == 0 && coord.z == 0) {
            world_chunk = it->second;
        }
    }
    collision_world = VoxelCollisionWorld(std::move(collision_chunks));
    rebuild_streamed_chunk_scene(world_chunk, scene);
}

void RuntimeWorldState::reset_streamed_chunks() {
    streamed_chunks.clear();
    chunk_mesh_cache_.clear();

    for (int chunk_z = -k_world_chunk_radius; chunk_z <= k_world_chunk_radius;
         ++chunk_z) {
        for (int chunk_x = -k_world_chunk_radius;
             chunk_x <= k_world_chunk_radius;
             ++chunk_x) {
            NetChunkCoord coord{};
            coord.x = static_cast<int16_t>(chunk_x);
            coord.z = static_cast<int16_t>(chunk_z);
            streamed_chunks[render_chunk_key(coord)] =
                RuntimeStreamedChunk{net_make_spherical_chunk_state(coord)};
        }
    }
}

void RuntimeWorldState::rebuild_streamed_chunk_scene(
    const VoxelChunk &world_chunk,
    RenderScene &scene) {
    (void)world_chunk;
    scene.opaque_meshes.clear();

    std::vector<NetChunkCoord> coords;
    coords.reserve(streamed_chunks.size());
    for (const auto &[key, chunk] : streamed_chunks) {
        (void)key;
        coords.push_back(chunk.state.coord);
    }
    std::sort(coords.begin(), coords.end(),
              [](const NetChunkCoord &a, const NetChunkCoord &b) {
                  if (a.z != b.z) {
                      return a.z < b.z;
                  }
                  return a.x < b.x;
              });

    for (const NetChunkCoord &coord : coords) {
        const int32_t key = render_chunk_key(coord);
        auto cache_it = chunk_mesh_cache_.find(key);
        if (cache_it != chunk_mesh_cache_.end()) {
            scene.opaque_meshes.push_back(cache_it->second);
            continue;
        }

        VoxelChunk render_chunk{};
        const RuntimeStreamedChunk &streamed_chunk =
            streamed_chunks.at(key);
        net_generate_chunk_from_state(render_chunk, streamed_chunk.state);
        const glm::vec3 chunk_origin(
            static_cast<float>(coord.x) *
                static_cast<float>(VoxelChunk::CHUNK_X),
            0.0f,
            static_cast<float>(coord.z) *
                static_cast<float>(VoxelChunk::CHUNK_Z));
        RenderMesh mesh = render_chunk.build_greedy_mesh(chunk_origin);
        mesh.mesh_id = next_mesh_id_++;
        chunk_mesh_cache_[key] = mesh;
        scene.opaque_meshes.push_back(mesh);
    }
}
