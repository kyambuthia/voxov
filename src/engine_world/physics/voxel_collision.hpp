#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <vector>

class VoxelChunk;

struct VoxelCollisionChunk {
    const VoxelChunk *chunk = nullptr;
    int32_t origin_x = 0;
    int32_t origin_z = 0;
};

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
    explicit VoxelCollisionWorld(const VoxelChunk *chunk_data = nullptr,
                                 float voxel_scale = 1.0f);
    explicit VoxelCollisionWorld(std::vector<VoxelCollisionChunk> chunks,
                                 float voxel_scale = 1.0f);

    void set_planet_surface_collider(glm::vec3 center, float radius);
    void set_planet_surface_collider(
        glm::vec3 center,
        float base_radius,
        float max_height_above_base,
        std::function<float(glm::vec3)> height_above_base_at_direction);

    bool is_solid_voxel(int x, int y, int z) const;
    bool raycast(glm::vec3 origin, glm::vec3 direction, float max_distance, float &out_hit_distance) const;
    bool has_planet_surface_collider() const { return has_planet_surface_collider_; }
    glm::vec3 planet_up_at(glm::vec3 world_position) const;
    bool planet_surface_point(glm::vec3 world_position,
                              glm::vec3 &out_surface_point,
                              glm::vec3 &out_up) const;

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

    const VoxelChunk *chunk_at(int x, int z, int &local_x, int &local_z) const;
    bool planet_surface_height(glm::vec2 xz, float &out_y) const;
    float planet_surface_radius_for_direction(glm::vec3 direction) const;
    float planet_surface_signed_distance(glm::vec3 world_position) const;
    bool raycast_planet_surface(glm::vec3 origin,
                                glm::vec3 direction,
                                float max_distance,
                                float &out_hit_distance) const;

    std::vector<VoxelCollisionChunk> chunks_;
    float voxel_scale_ = 1.0f;
    bool has_planet_surface_collider_ = false;
    glm::vec3 planet_surface_center_ = glm::vec3(0.0f);
    float planet_surface_radius_ = 0.0f;
    float planet_surface_base_radius_ = 0.0f;
    float planet_surface_max_height_above_base_ = 0.0f;
    std::function<float(glm::vec3)> planet_surface_height_at_direction_;
};
