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

bool VoxelCollisionWorld::segment_intersects_aabb(glm::vec3 a, glm::vec3 b, glm::vec3 bmin, glm::vec3 bmax) const {
    const glm::vec3 d = b - a;
    float tmin = 0.0f;
    float tmax = 1.0f;

    for (int axis = 0; axis < 3; ++axis) {
        if (std::fabs(d[axis]) < 1e-6f) {
            if (a[axis] < bmin[axis] || a[axis] > bmax[axis]) {
                return false;
            }
            continue;
        }

        const float inv = 1.0f / d[axis];
        float t1 = (bmin[axis] - a[axis]) * inv;
        float t2 = (bmax[axis] - a[axis]) * inv;
        if (t1 > t2) {
            std::swap(t1, t2);
        }

        tmin = std::max(tmin, t1);
        tmax = std::min(tmax, t2);
        if (tmin > tmax) {
            return false;
        }
    }

    return true;
}

bool VoxelCollisionWorld::capsule_overlaps(glm::vec3 feet_position, float capsule_radius, float capsule_height) const {
    if (!chunk) {
        return false;
    }

    const float lower_center_y = feet_position.y + capsule_radius;
    const float upper_center_y = feet_position.y + std::max(capsule_radius, capsule_height - capsule_radius);
    const glm::vec3 seg_a(feet_position.x, lower_center_y, feet_position.z);
    const glm::vec3 seg_b(feet_position.x, upper_center_y, feet_position.z);

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

                glm::vec3 bmin(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
                glm::vec3 bmax = bmin + glm::vec3(1.0f);

                bmin -= glm::vec3(capsule_radius);
                bmax += glm::vec3(capsule_radius);

                if (segment_intersects_aabb(seg_a, seg_b, bmin, bmax)) {
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
        spawn_y += 0.1f;
    }

    return spawn_y;
}
