#pragma once

#include "engine_physics/vehicle/voxel_vehicle_builder.hpp"

#include <vector>

#include <glm/glm.hpp>

struct VoxelDamageCell {
    glm::ivec3 cell = glm::ivec3(0);
    float hit_points = 100.0f;
    bool destroyed = false;
};

struct VoxelDamageStats {
    float integrity = 1.0f;
    float mass_scale = 1.0f;
    uint32_t destroyed_cells = 0;
};

class VehicleDamageModel {
public:
    void initialize(const std::vector<VoxelMassCell> &cells, float base_hit_points);
    void apply_impact(const glm::vec3 &local_impact_point, float energy_joules, float radius_meters);
    void repair_all();

    const std::vector<VoxelDamageCell> &cells() const;
    const VoxelDamageStats &stats() const;

private:
    void recalculate_stats();

    std::vector<VoxelDamageCell> damage_cells;
    VoxelDamageStats current{};
};
