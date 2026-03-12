#pragma once

#include "engine_net/net_client.hpp"
#include "engine_net/net_common.hpp"
#include "engine_render/render_types.hpp"
#include "engine_world/physics/voxel_collision.hpp"
#include "engine_world/voxel_chunk.hpp"
#include "platform/platform_services.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct RuntimeStreamedChunk {
    NetChunkState state{};
};

struct RuntimeObjectiveNode {
    glm::vec3 position = glm::vec3(0.0f);
    float interact_radius = 2.4f;
    bool activated = false;
};

class RuntimeWorldState {
public:
    void initialize(
        bool spherical_planet,
        VoxelChunk &world_chunk,
        VoxelCollisionWorld &collision_world,
        RenderScene &scene);
    void reset_streamed_chunks(bool spherical_planet);
    bool consume_chunk_stream_updates(
        NetClient &net_client,
        const NetChunkInterest &last_chunk_interest,
        bool has_last_chunk_interest,
        bool spherical_planet,
        uint32_t &out_packet_count,
        uint32_t &out_change_count);
    void rebuild_streamed_chunk_scene(
        const VoxelChunk &world_chunk,
        RenderScene &scene,
        bool spherical_planet) const;
    void load_persistent_state(const PlatformServices &platform_services);
    void save_persistent_state(const PlatformServices &platform_services) const;

    std::unordered_map<int32_t, RuntimeStreamedChunk> streamed_chunks;
    std::vector<RuntimeObjectiveNode> objective_nodes;
    int nearby_objective_node = -1;
    int activated_objective_count = 0;
    bool extraction_unlocked = false;
    bool objective_round_complete = false;
    glm::vec3 extraction_zone_position = glm::vec3(0.0f);
    float extraction_zone_radius = 3.2f;
    std::string objective_hint;
    glm::vec3 spherical_planet_center = glm::vec3(0.0f);
    float spherical_planet_radius = 0.0f;
};
