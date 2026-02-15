#pragma once

#include <glm/glm.hpp>

class VoxelChunk;

class VoxelCollisionWorld {
public:
    explicit VoxelCollisionWorld(const VoxelChunk *chunk_data);

    bool is_solid_voxel(int x, int y, int z) const;
    bool capsule_overlaps(glm::vec3 feet_position, float capsule_radius, float capsule_height) const;
    bool raycast(glm::vec3 origin, glm::vec3 direction, float max_distance, float &out_hit_distance) const;

    float find_spawn_height(glm::vec2 xz, float capsule_radius, float capsule_height) const;

private:
    bool segment_intersects_aabb(glm::vec3 a, glm::vec3 b, glm::vec3 bmin, glm::vec3 bmax) const;

    const VoxelChunk *chunk = nullptr;
};
