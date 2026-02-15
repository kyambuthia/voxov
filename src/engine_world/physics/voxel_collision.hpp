#pragma once

#include <glm/glm.hpp>

#include <vector>

class VoxelChunk;

struct CapsuleResolveResult {
    glm::vec3 position = glm::vec3(0.0f);
    bool had_collision = false;
    float total_correction = 0.0f;
    glm::vec3 contact_normal = glm::vec3(0.0f, 1.0f, 0.0f);
    std::vector<glm::ivec3> overlapped_voxels;

    bool grounded = false;
    float ground_distance = 0.0f;
    glm::vec3 ground_ray_origin = glm::vec3(0.0f);
    glm::vec3 ground_ray_hit = glm::vec3(0.0f);
};

class VoxelCollisionWorld {
public:
    explicit VoxelCollisionWorld(const VoxelChunk *chunk_data);

    bool is_solid_voxel(int x, int y, int z) const;
    bool raycast(glm::vec3 origin, glm::vec3 direction, float max_distance, float &out_hit_distance) const;

    CapsuleResolveResult resolve_capsule(
        glm::vec3 feet_position,
        float capsule_radius,
        float capsule_height,
        float skin_width,
        int max_iterations,
        float max_correction_per_frame) const;

    float find_spawn_height(glm::vec2 xz, float capsule_radius, float capsule_height) const;

private:
    bool capsule_overlaps(glm::vec3 feet_position, float capsule_radius, float capsule_height) const;
    bool segment_intersects_aabb(glm::vec3 a, glm::vec3 b, glm::vec3 bmin, glm::vec3 bmax) const;

    const VoxelChunk *chunk = nullptr;
};
