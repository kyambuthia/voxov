#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

struct VoxelMassMaterial {
    float density_kg_per_m3 = 550.0f;
};

struct VoxelMassCell {
    glm::ivec3 cell = glm::ivec3(0);
    VoxelMassMaterial material{};
};

struct VoxelWheelMount {
    glm::vec3 local_position = glm::vec3(0.0f);
};

struct VoxelVehicleMassProperties {
    float total_mass_kg = 0.0f;
    glm::vec3 center_of_mass = glm::vec3(0.0f);
    glm::vec3 inertia_diagonal = glm::vec3(0.0f);
    glm::vec3 local_bounds_min = glm::vec3(0.0f);
    glm::vec3 local_bounds_max = glm::vec3(0.0f);
};

struct VoxelVehicleBuildResult {
    VoxelVehicleMassProperties mass{};
    std::vector<VoxelWheelMount> wheel_mounts;
};

VoxelVehicleBuildResult build_voxel_vehicle_properties(
    const std::vector<VoxelMassCell> &cells,
    float voxel_size_meters);
