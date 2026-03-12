#include "engine_runtime/runtime_world_state.hpp"

#include "engine_net/net_runtime_shared.hpp"
#include "engine_world/world_gen.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

namespace {
constexpr uint64_t k_session_state_magic = 0x564F585356303031ull;

struct SavedSessionState {
    uint64_t magic = k_session_state_magic;
    uint64_t world_seed = k_voxov_flat_world_seed;
    uint8_t objective_count = 0;
    uint8_t activated_mask = 0;
    uint8_t extraction_unlocked = 0;
    uint8_t objective_round_complete = 0;
};

int32_t render_chunk_key(NetChunkCoord coord) {
    return (static_cast<int32_t>(coord.x) << 16) ^
        static_cast<uint16_t>(coord.z);
}
} // namespace

void RuntimeWorldState::initialize(
    bool spherical_planet,
    VoxelChunk &world_chunk,
    VoxelCollisionWorld &collision_world,
    RenderScene &scene) {
    if (spherical_planet) {
        world_chunk.generate_spherical_planet_seeded(k_voxov_flat_world_seed);
        spherical_planet_center = glm::vec3(
            static_cast<float>(VoxelChunk::CHUNK_X - 1) * 0.5f,
            static_cast<float>(VoxelChunk::CHUNK_Y - 1) * 0.42f,
            static_cast<float>(VoxelChunk::CHUNK_Z - 1) * 0.5f);
        spherical_planet_radius = static_cast<float>(std::min(
            {VoxelChunk::CHUNK_X, VoxelChunk::CHUNK_Y, VoxelChunk::CHUNK_Z})) *
            0.34f;
    } else {
        generate_flat_world_locomotion_chunk(world_chunk);
        spherical_planet_center = glm::vec3(0.0f);
        spherical_planet_radius = 0.0f;
    }
    collision_world = VoxelCollisionWorld(&world_chunk);

    scene = RenderScene{};
    reset_streamed_chunks(spherical_planet);
    rebuild_streamed_chunk_scene(world_chunk, scene, spherical_planet);
    scene.debug_grid = world_chunk.build_debug_grid(160.0f, 1.0f);

    const glm::vec2 center(
        static_cast<float>(VoxelChunk::CHUNK_X) * 0.5f,
        static_cast<float>(VoxelChunk::CHUNK_Z) * 0.5f);

    objective_nodes.clear();
    activated_objective_count = 0;
    extraction_unlocked = false;
    objective_round_complete = false;
    nearby_objective_node = -1;
    objective_hint.clear();
    extraction_zone_position = glm::vec3(center.x, 0.0f, center.y + 18.0f);
    extraction_zone_position.y = collision_world.find_spawn_height(
        glm::vec2(extraction_zone_position.x, extraction_zone_position.z),
        0.45f,
        1.8f) +
        0.05f;

    const auto spawn_objective_node = [&](const glm::vec3 &base) {
        RuntimeObjectiveNode node{};
        node.position = base;
        node.position.y = collision_world.find_spawn_height(
                              glm::vec2(base.x, base.z), 0.45f, 1.8f) +
            0.05f;
        node.interact_radius = 2.6f;
        node.activated = false;
        objective_nodes.push_back(node);
    };
    if (spherical_planet) {
        spawn_objective_node(glm::vec3(center.x - 12.0f, 0.0f, center.y - 10.0f));
        spawn_objective_node(glm::vec3(center.x + 14.0f, 0.0f, center.y - 4.0f));
        spawn_objective_node(glm::vec3(center.x + 2.0f, 0.0f, center.y + 14.0f));
    } else {
        spawn_objective_node(glm::vec3(center.x - 14.0f, 0.0f, center.y - 10.0f));
        spawn_objective_node(glm::vec3(center.x + 13.0f, 0.0f, center.y - 6.0f));
        spawn_objective_node(glm::vec3(center.x + 4.0f, 0.0f, center.y + 15.0f));
    }
}

void RuntimeWorldState::reset_streamed_chunks(bool spherical_planet) {
    streamed_chunks.clear();
    if (spherical_planet) {
        return;
    }

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

bool RuntimeWorldState::consume_chunk_stream_updates(
    NetClient &net_client,
    const NetChunkInterest &last_chunk_interest,
    bool has_last_chunk_interest,
    bool spherical_planet,
    uint32_t &out_packet_count,
    uint32_t &out_change_count) {
    out_packet_count = 0;
    out_change_count = 0;
    bool changed = false;
    NetChunkState update{};
    while (net_client.poll_chunk_state(update)) {
        out_packet_count += 1;
        const int32_t key = render_chunk_key(update.coord);
        auto it = streamed_chunks.find(key);
        if (it != streamed_chunks.end() &&
            net_chunk_state_matches(it->second.state, update)) {
            continue;
        }
        streamed_chunks[key] = RuntimeStreamedChunk{update};
        changed = true;
        out_change_count += 1;
    }

    if (!spherical_planet && has_last_chunk_interest) {
        std::vector<int32_t> stale_keys;
        stale_keys.reserve(streamed_chunks.size());
        for (const auto &[key, chunk] : streamed_chunks) {
            const int32_t dx = std::abs(
                static_cast<int32_t>(chunk.state.coord.x) -
                last_chunk_interest.center_x);
            const int32_t dz = std::abs(
                static_cast<int32_t>(chunk.state.coord.z) -
                last_chunk_interest.center_z);
            if (dx > static_cast<int32_t>(last_chunk_interest.radius) ||
                dz > static_cast<int32_t>(last_chunk_interest.radius)) {
                stale_keys.push_back(key);
            }
        }
        for (int32_t key : stale_keys) {
            streamed_chunks.erase(key);
            changed = true;
            out_change_count += 1;
        }
    }

    return changed;
}

void RuntimeWorldState::rebuild_streamed_chunk_scene(
    const VoxelChunk &world_chunk,
    RenderScene &scene,
    bool spherical_planet) const {
    scene.opaque_meshes.clear();
    scene.opaque_meshes.push_back(world_chunk.build_sky_placeholder(240.0f));
    if (spherical_planet) {
        scene.opaque_meshes.push_back(world_chunk.build_naive_mesh());
        return;
    }

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
        VoxelChunk render_chunk{};
        const RuntimeStreamedChunk &streamed_chunk =
            streamed_chunks.at(render_chunk_key(coord));
        net_generate_chunk_from_state(render_chunk, streamed_chunk.state);
        const glm::vec3 chunk_origin(
            static_cast<float>(coord.x) *
                static_cast<float>(VoxelChunk::CHUNK_X),
            0.0f,
            static_cast<float>(coord.z) *
                static_cast<float>(VoxelChunk::CHUNK_Z));
        scene.opaque_meshes.push_back(
            render_chunk.build_naive_mesh(chunk_origin));
    }
}

void RuntimeWorldState::load_persistent_state(
    const PlatformServices &platform_services) {
    const std::filesystem::path path = platform_services.session_state_path();
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return;
    }

    SavedSessionState state{};
    in.read(reinterpret_cast<char *>(&state), sizeof(state));
    if (!in || state.magic != k_session_state_magic ||
        state.world_seed != k_voxov_flat_world_seed) {
        return;
    }

    activated_objective_count = 0;
    for (size_t i = 0; i < objective_nodes.size(); ++i) {
        const bool activated =
            (state.activated_mask & static_cast<uint8_t>(1u << i)) != 0;
        objective_nodes[i].activated = activated;
        activated_objective_count += activated ? 1 : 0;
    }
    extraction_unlocked = state.extraction_unlocked != 0;
    objective_round_complete = state.objective_round_complete != 0;
}

void RuntimeWorldState::save_persistent_state(
    const PlatformServices &platform_services) const {
    const std::filesystem::path path = platform_services.session_state_path();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    SavedSessionState state{};
    state.objective_count =
        static_cast<uint8_t>(std::min<size_t>(objective_nodes.size(), 8));
    for (size_t i = 0; i < objective_nodes.size() && i < 8; ++i) {
        if (objective_nodes[i].activated) {
            state.activated_mask |= static_cast<uint8_t>(1u << i);
        }
    }
    state.extraction_unlocked = extraction_unlocked ? 1 : 0;
    state.objective_round_complete = objective_round_complete ? 1 : 0;

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return;
    }
    out.write(reinterpret_cast<const char *>(&state), sizeof(state));
}
