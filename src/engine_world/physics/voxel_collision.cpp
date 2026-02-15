#include "engine_world/physics/voxel_collision.hpp"

#include "engine_world/voxel_chunk.hpp"

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

VoxelCollisionWorld::VoxelCollisionWorld(const VoxelChunk *chunk_data)
    : chunk(chunk_data) {}

bool VoxelCollisionWorld::is_solid_voxel(int x, int y, int z) const {
    if (!chunk) {
        return false;
    }
    return chunk->solid(x, y, z);
}

bool VoxelCollisionWorld::sphere_overlaps_box(glm::vec3 center, float radius, glm::vec3 bmin, glm::vec3 bmax) const {
    glm::vec3 p(
        std::clamp(center.x, bmin.x, bmax.x),
        std::clamp(center.y, bmin.y, bmax.y),
        std::clamp(center.z, bmin.z, bmax.z));
    const glm::vec3 d = center - p;
    return glm::dot(d, d) <= (radius * radius);
}

bool VoxelCollisionWorld::capsule_overlaps(glm::vec3 feet_position, float capsule_radius, float capsule_height) const {
    if (!chunk) {
        return false;
    }

    const float lower_y = feet_position.y + capsule_radius;
    const float upper_y = feet_position.y + std::max(capsule_radius, capsule_height - capsule_radius);

    const int min_x = static_cast<int>(std::floor(feet_position.x - capsule_radius));
    const int max_x = static_cast<int>(std::floor(feet_position.x + capsule_radius));
    const int min_y = static_cast<int>(std::floor(feet_position.y));
    const int max_y = static_cast<int>(std::floor(feet_position.y + capsule_height));
    const int min_z = static_cast<int>(std::floor(feet_position.z - capsule_radius));
    const int max_z = static_cast<int>(std::floor(feet_position.z + capsule_radius));

    for (int z = min_z; z <= max_z; ++z) {
        for (int y = min_y; y <= max_y; ++y) {
            for (int x = min_x; x <= max_x; ++x) {
                if (!is_solid_voxel(x, y, z)) {
                    continue;
                }

                const glm::vec3 bmin(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
                const glm::vec3 bmax = bmin + glm::vec3(1.0f);

                const float sample_y = std::clamp(bmin.y + 0.5f, lower_y, upper_y);
                const glm::vec3 sphere_center(feet_position.x, sample_y, feet_position.z);
                if (sphere_overlaps_box(sphere_center, capsule_radius, bmin, bmax)) {
                    return true;
                }
            }
        }
    }

    return false;
}

bool VoxelCollisionWorld::raycast(glm::vec3 origin, glm::vec3 direction, float max_distance, float &out_hit_distance) const {
    if (!chunk) {
        return false;
    }

    const float len = glm::length(direction);
    if (len < 0.0001f) {
        return false;
    }

    direction /= len;
    const float step = 0.1f;
    float d = 0.0f;
    while (d <= max_distance) {
        const glm::vec3 p = origin + direction * d;
        const int vx = static_cast<int>(std::floor(p.x));
        const int vy = static_cast<int>(std::floor(p.y));
        const int vz = static_cast<int>(std::floor(p.z));
        if (is_solid_voxel(vx, vy, vz)) {
            out_hit_distance = d;
            return true;
        }
        d += step;
    }

    return false;
}

float VoxelCollisionWorld::find_spawn_height(glm::vec2 xz, float capsule_radius, float capsule_height) const {
    float spawn_y = 8.0f;
    const int vx = static_cast<int>(std::floor(xz.x));
    const int vz = static_cast<int>(std::floor(xz.y));

    for (int y = VoxelChunk::CHUNK_Y - 1; y >= 0; --y) {
        if (is_solid_voxel(vx, y, vz)) {
            spawn_y = static_cast<float>(y + 1);
            break;
        }
    }

    while (capsule_overlaps(glm::vec3(xz.x, spawn_y, xz.y), capsule_radius, capsule_height)) {
        spawn_y += 0.2f;
    }

    return spawn_y;
}
