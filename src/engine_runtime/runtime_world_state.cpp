#include "engine_runtime/runtime_world_state.hpp"

#include "engine_net/net_runtime_shared.hpp"
#include "engine_world/world_gen.hpp"

#include <algorithm>
#include <cmath>

namespace {
constexpr uint64_t k_session_state_magic = 0x564F585356303031ull;

struct SavedSessionState {
    uint64_t magic = k_session_state_magic;
    uint64_t world_seed = k_voxov_flat_world_seed;
};

int32_t render_chunk_key(NetChunkCoord coord) {
    return (static_cast<int32_t>(coord.x) << 16) ^
        static_cast<uint16_t>(coord.z);
}

void add_flat_world_boundary_walls(VoxelChunk &chunk) {
    constexpr int kWallTop = 18;
    for (int y = 0; y <= kWallTop; ++y) {
        for (int x = 0; x < VoxelChunk::CHUNK_X; ++x) {
            chunk.set_material(x, y, 0, VoxelMaterial::Stone);
            chunk.set_material(x, y, VoxelChunk::CHUNK_Z - 1, VoxelMaterial::Stone);
        }
        for (int z = 0; z < VoxelChunk::CHUNK_Z; ++z) {
            chunk.set_material(0, y, z, VoxelMaterial::Stone);
            chunk.set_material(VoxelChunk::CHUNK_X - 1, y, z, VoxelMaterial::Stone);
        }
    }
    chunk.refresh_surface_materials();
}
} // namespace

void RuntimeWorldState::initialize(
    VoxelChunk &world_chunk,
    VoxelCollisionWorld &collision_world,
    RenderScene &scene) {
    generate_flat_world_locomotion_chunk(world_chunk);
    add_flat_world_boundary_walls(world_chunk);
    collision_world = VoxelCollisionWorld(&world_chunk);

    scene = RenderScene{};
    reset_streamed_chunks();
    rebuild_streamed_chunk_scene(world_chunk, scene);

}

void RuntimeWorldState::reset_streamed_chunks() {
    streamed_chunks.clear();
    chunk_mesh_cache_.clear();

    constexpr int k_render_chunk_radius = 1;
    for (int chunk_z = -k_render_chunk_radius; chunk_z <= k_render_chunk_radius;
         ++chunk_z) {
        for (int chunk_x = -k_render_chunk_radius;
             chunk_x <= k_render_chunk_radius;
             ++chunk_x) {
            NetChunkCoord coord{};
            coord.x = static_cast<int16_t>(chunk_x);
            coord.z = static_cast<int16_t>(chunk_z);
            streamed_chunks[render_chunk_key(coord)] =
                RuntimeStreamedChunk{net_make_flat_chunk_state(coord)};
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

void RuntimeWorldState::load_persistent_state(
    const PlatformServices &platform_services) {
    (void)platform_services;
}

void RuntimeWorldState::save_persistent_state(
    const PlatformServices &platform_services) const {
    (void)platform_services;
}
