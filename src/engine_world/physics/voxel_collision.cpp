#include "engine_world/physics/voxel_collision.hpp"

#include "engine_world/voxel_chunk.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

VoxelCollisionWorld::VoxelCollisionWorld(const VoxelChunk *chunk_data,
                                         float voxel_scale)
    : voxel_scale_(voxel_scale > 0.0f ? voxel_scale : 1.0f) {
    if (chunk_data != nullptr) {
        chunks_.push_back(VoxelCollisionChunk{chunk_data, 0, 0});
    }
}

VoxelCollisionWorld::VoxelCollisionWorld(std::vector<VoxelCollisionChunk> chunks,
                                         float voxel_scale)
    : chunks_(std::move(chunks)),
      voxel_scale_(voxel_scale > 0.0f ? voxel_scale : 1.0f) {}

void VoxelCollisionWorld::set_planet_surface_collider(glm::vec3 center, float radius) {
    has_planet_surface_collider_ = radius > 0.0f;
    planet_surface_center_ = center;
    planet_surface_radius_ = std::max(0.0f, radius);
    planet_surface_base_radius_ = planet_surface_radius_;
    planet_surface_max_height_above_base_ = 0.0f;
    planet_surface_height_at_direction_ = nullptr;
}

void VoxelCollisionWorld::set_planet_surface_collider(
    glm::vec3 center,
    float base_radius,
    float max_height_above_base,
    std::function<float(glm::vec3)> height_above_base_at_direction) {
    has_planet_surface_collider_ = base_radius > 0.0f;
    planet_surface_center_ = center;
    planet_surface_base_radius_ = std::max(0.0f, base_radius);
    planet_surface_max_height_above_base_ = std::max(0.0f, max_height_above_base);
    planet_surface_radius_ =
        planet_surface_base_radius_ + planet_surface_max_height_above_base_;
    planet_surface_height_at_direction_ = std::move(height_above_base_at_direction);
}

const VoxelChunk *VoxelCollisionWorld::chunk_at(int x, int z, int &local_x, int &local_z) const {
    for (const VoxelCollisionChunk &entry : chunks_) {
        if (entry.chunk == nullptr) {
            continue;
        }
        const int rel_x = x - entry.origin_x;
        const int rel_z = z - entry.origin_z;
        if (rel_x >= 0 && rel_x < VoxelChunk::CHUNK_X &&
            rel_z >= 0 && rel_z < VoxelChunk::CHUNK_Z) {
            local_x = rel_x;
            local_z = rel_z;
            return entry.chunk;
        }
    }
    return nullptr;
}

bool VoxelCollisionWorld::planet_surface_height(glm::vec2 xz, float &out_y) const {
    if (!has_planet_surface_collider_ || planet_surface_radius_ <= 0.0f) {
        return false;
    }

    const float dx = xz.x - planet_surface_center_.x;
    const float dz = xz.y - planet_surface_center_.z;
    const float horizontal_sq = dx * dx + dz * dz;

    float surface_radius = planet_surface_radius_;
    if (planet_surface_height_at_direction_) {
        const float broad_radius = planet_surface_radius_;
        if (horizontal_sq > broad_radius * broad_radius) {
            return false;
        }

        float direction_y = std::sqrt(std::max(
            0.0f, planet_surface_base_radius_ * planet_surface_base_radius_ -
                      horizontal_sq));
        for (int i = 0; i < 2; ++i) {
            const glm::vec3 direction =
                glm::normalize(glm::vec3(dx, direction_y, dz));
            const float height = std::clamp(planet_surface_height_at_direction_(direction),
                                            0.0f,
                                            planet_surface_max_height_above_base_);
            surface_radius = planet_surface_base_radius_ + height;
            if (horizontal_sq > surface_radius * surface_radius) {
                return false;
            }
            direction_y = std::sqrt(std::max(0.0f,
                                             surface_radius * surface_radius -
                                                 horizontal_sq));
        }
    }

    const float radius_sq = surface_radius * surface_radius;
    if (horizontal_sq > radius_sq) {
        return false;
    }

    out_y = planet_surface_center_.y +
            std::sqrt(std::max(0.0f, radius_sq - horizontal_sq));
    return true;
}

bool VoxelCollisionWorld::is_solid_voxel(int x, int y, int z) const {
    int local_x = 0;
    int local_z = 0;
    const VoxelChunk *chunk = chunk_at(x, z, local_x, local_z);
    if (chunk == nullptr) {
        return false;
    }
    return chunk->solid(local_x, y, local_z);
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
    const float lower_center_y = feet_position.y + capsule_radius;
    const float upper_center_y = feet_position.y + std::max(capsule_radius, capsule_height - capsule_radius);
    const glm::vec3 seg_a(feet_position.x, lower_center_y, feet_position.z);
    const glm::vec3 seg_b(feet_position.x, upper_center_y, feet_position.z);

    const float inv_scale = 1.0f / voxel_scale_;
    const int min_x = static_cast<int>(std::floor((feet_position.x - capsule_radius) * inv_scale));
    const int max_x = static_cast<int>(std::floor((feet_position.x + capsule_radius) * inv_scale));
    const int min_y = static_cast<int>(std::floor(feet_position.y * inv_scale));
    const int max_y = static_cast<int>(std::floor((feet_position.y + capsule_height) * inv_scale));
    const int min_z = static_cast<int>(std::floor((feet_position.z - capsule_radius) * inv_scale));
    const int max_z = static_cast<int>(std::floor((feet_position.z + capsule_radius) * inv_scale));

    for (int z = min_z; z <= max_z; ++z) {
        for (int y = min_y; y <= max_y; ++y) {
            for (int x = min_x; x <= max_x; ++x) {
                if (!is_solid_voxel(x, y, z)) {
                    continue;
                }

                glm::vec3 bmin = glm::vec3(x, y, z) * voxel_scale_;
                glm::vec3 bmax = bmin + glm::vec3(voxel_scale_);

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

CapsuleResolveResult VoxelCollisionWorld::resolve_capsule(
    glm::vec3 feet_position,
    float capsule_radius,
    float capsule_height,
    float skin_width,
    int max_iterations,
    float max_correction_per_frame) const {
    CapsuleResolveResult result{};
    result.position = feet_position;

    const float lower_center_offset = capsule_radius;
    const float upper_center_offset = std::max(capsule_radius, capsule_height - capsule_radius);

    float correction_budget = std::max(0.0f, max_correction_per_frame);

    for (int iter = 0; iter < max_iterations; ++iter) {
        const glm::vec3 seg_a(result.position.x, result.position.y + lower_center_offset, result.position.z);
        const glm::vec3 seg_b(result.position.x, result.position.y + upper_center_offset, result.position.z);

        const float inv_scale = 1.0f / voxel_scale_;
        const int min_x = static_cast<int>(std::floor((result.position.x - capsule_radius - skin_width) * inv_scale));
        const int max_x = static_cast<int>(std::floor((result.position.x + capsule_radius + skin_width) * inv_scale));
        const int min_y = static_cast<int>(std::floor((result.position.y - skin_width) * inv_scale));
        const int max_y = static_cast<int>(std::floor((result.position.y + capsule_height + skin_width) * inv_scale));
        const int min_z = static_cast<int>(std::floor((result.position.z - capsule_radius - skin_width) * inv_scale));
        const int max_z = static_cast<int>(std::floor((result.position.z + capsule_radius + skin_width) * inv_scale));

        bool had_penetration = false;
        glm::vec3 total_push(0.0f);

        for (int z = min_z; z <= max_z; ++z) {
            for (int y = min_y; y <= max_y; ++y) {
                for (int x = min_x; x <= max_x; ++x) {
                    if (!is_solid_voxel(x, y, z)) {
                        continue;
                    }

                    const glm::vec3 box_min = glm::vec3(x, y, z) * voxel_scale_;
                    const glm::vec3 box_max = box_min + glm::vec3(voxel_scale_);

                    float closest_t = 0.0f;
                    float best_sq = std::numeric_limits<float>::max();
                    for (int s = 0; s <= 4; ++s) {
                        const float t = static_cast<float>(s) / 4.0f;
                        const glm::vec3 p = seg_a + (seg_b - seg_a) * t;
                        const glm::vec3 q(
                            std::clamp(p.x, box_min.x, box_max.x),
                            std::clamp(p.y, box_min.y, box_max.y),
                            std::clamp(p.z, box_min.z, box_max.z));
                        const glm::vec3 diff = p - q;
                        const float sq = glm::dot(diff, diff);
                        if (sq < best_sq) {
                            best_sq = sq;
                            closest_t = t;
                        }
                    }

                    const glm::vec3 p = seg_a + (seg_b - seg_a) * closest_t;
                    const glm::vec3 q(
                        std::clamp(p.x, box_min.x, box_max.x),
                        std::clamp(p.y, box_min.y, box_max.y),
                        std::clamp(p.z, box_min.z, box_max.z));

                    glm::vec3 n = p - q;
                    float dist = glm::length(n);
                    const float target_dist = capsule_radius + skin_width;
                    const float penetration = target_dist - dist;

                    if (penetration <= 0.0f) {
                        continue;
                    }

                    had_penetration = true;
                    result.had_collision = true;
                    result.overlapped_voxels.push_back(glm::ivec3(x, y, z));

                    if (dist > 1e-5f) {
                        n /= dist;
                    } else {
                        const glm::vec3 c = (box_min + box_max) * 0.5f;
                        const glm::vec3 d = p - c;
                        const glm::vec3 ad = glm::abs(d);
                        if (ad.x >= ad.y && ad.x >= ad.z) {
                            n = glm::vec3(d.x >= 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f);
                        } else if (ad.y >= ad.x && ad.y >= ad.z) {
                            n = glm::vec3(0.0f, d.y >= 0.0f ? 1.0f : -1.0f, 0.0f);
                        } else {
                            n = glm::vec3(0.0f, 0.0f, d.z >= 0.0f ? 1.0f : -1.0f);
                        }
                    }

                    total_push += n * penetration;
                }
            }
        }

        if (!had_penetration) {
            break;
        }

        if (glm::dot(total_push, total_push) <= 1e-8f) {
            break;
        }

        float push_len = glm::length(total_push);
        if (correction_budget > 0.0f && push_len > correction_budget) {
            total_push *= (correction_budget / push_len);
            push_len = correction_budget;
        }

        result.position += total_push;
        result.total_correction += push_len;
        result.contact_normal = glm::normalize(total_push);

        if (correction_budget > 0.0f) {
            correction_budget = std::max(0.0f, correction_budget - push_len);
        }
    }

    float surface_y = 0.0f;
    if (planet_surface_height(glm::vec2(result.position.x, result.position.z),
                              surface_y)) {
        const float min_feet_y = surface_y + skin_width;
        if (result.position.y < min_feet_y) {
            const float correction = min_feet_y - result.position.y;
            result.position.y += correction;
            result.had_collision = true;
            result.total_correction += correction;
            result.contact_normal = glm::vec3(0.0f, 1.0f, 0.0f);
        }
    }

    const float ground_probe_dist = std::max(0.12f, skin_width + 0.03f);
    result.ground_ray_origin = result.position + glm::vec3(0.0f, skin_width + 0.02f, 0.0f);
    float hit_distance = 0.0f;
    if (raycast(result.ground_ray_origin, glm::vec3(0.0f, -1.0f, 0.0f), ground_probe_dist, hit_distance)) {
        result.grounded = true;
        result.ground_distance = hit_distance;
        result.ground_ray_hit = result.ground_ray_origin + glm::vec3(0.0f, -hit_distance, 0.0f);
    } else {
        result.grounded = false;
        result.ground_distance = ground_probe_dist;
        result.ground_ray_hit = result.ground_ray_origin + glm::vec3(0.0f, -ground_probe_dist, 0.0f);
    }

    return result;
}

bool VoxelCollisionWorld::raycast(glm::vec3 origin, glm::vec3 direction, float max_distance, float &out_hit_distance) const {
    const float len = glm::length(direction);
    if (len < 0.0001f) {
        return false;
    }

    direction /= len;
    bool hit = false;
    out_hit_distance = max_distance;

    if (has_planet_surface_collider_ && planet_surface_radius_ > 0.0f) {
        float surface_y = 0.0f;
        if (std::fabs(direction.y) > 0.0001f &&
            planet_surface_height(glm::vec2(origin.x, origin.z), surface_y)) {
            const float t = (surface_y - origin.y) / direction.y;
            if (t >= 0.0f && t <= max_distance) {
                out_hit_distance = t;
                hit = true;
            }
        }

        if (!planet_surface_height_at_direction_) {
            const glm::vec3 oc = origin - planet_surface_center_;
            const float b = 2.0f * glm::dot(oc, direction);
            const float c = glm::dot(oc, oc) -
                            planet_surface_radius_ * planet_surface_radius_;
            const float discriminant = b * b - 4.0f * c;
            if (discriminant >= 0.0f) {
                const float root = std::sqrt(discriminant);
                const float t0 = (-b - root) * 0.5f;
                const float t1 = (-b + root) * 0.5f;
                const float t = t0 >= 0.0f ? t0 : t1;
                if (t >= 0.0f && t <= max_distance) {
                    out_hit_distance = t;
                    hit = true;
                }
            }
        }
    }

    if (chunks_.empty()) {
        return hit;
    }

    const float step = voxel_scale_ * 0.05f;
    float d = 0.0f;
    while (d <= std::min(max_distance, out_hit_distance)) {
        const glm::vec3 p = origin + direction * d;
        const float inv_scale = 1.0f / voxel_scale_;
        const int vx = static_cast<int>(std::floor(p.x * inv_scale));
        const int vy = static_cast<int>(std::floor(p.y * inv_scale));
        const int vz = static_cast<int>(std::floor(p.z * inv_scale));
        if (is_solid_voxel(vx, vy, vz)) {
            out_hit_distance = d;
            return true;
        }
        d += step;
    }

    return hit;
}

float VoxelCollisionWorld::find_spawn_height(glm::vec2 xz, float capsule_radius, float capsule_height) const {
    float spawn_y = 8.0f * voxel_scale_;
    float planet_y = 0.0f;
    if (planet_surface_height(xz, planet_y)) {
        spawn_y = std::max(spawn_y, planet_y);
    }
    const float inv_scale = 1.0f / voxel_scale_;
    const int vx = static_cast<int>(std::floor(xz.x * inv_scale));
    const int vz = static_cast<int>(std::floor(xz.y * inv_scale));

    for (int y = VoxelChunk::CHUNK_Y - 1; y >= 0; --y) {
        if (is_solid_voxel(vx, y, vz)) {
            spawn_y = (static_cast<float>(y) + 1.0f) * voxel_scale_;
            break;
        }
    }

    while (capsule_overlaps(glm::vec3(xz.x, spawn_y, xz.y), capsule_radius, capsule_height)) {
        spawn_y += voxel_scale_ * 0.1f;
    }

    return spawn_y;
}
