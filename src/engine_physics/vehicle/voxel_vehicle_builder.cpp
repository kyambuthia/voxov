#include "engine_physics/vehicle/voxel_vehicle_builder.hpp"

#include <algorithm>

namespace {
glm::vec3 cell_center(const glm::ivec3 &cell, float voxel_size_meters) {
    const glm::vec3 c = glm::vec3(cell);
    return (c + glm::vec3(0.5f)) * voxel_size_meters;
}
}

VoxelVehicleBuildResult build_voxel_vehicle_properties(
    const std::vector<VoxelMassCell> &cells,
    float voxel_size_meters) {
    VoxelVehicleBuildResult out{};
    if (cells.empty() || voxel_size_meters <= 0.0f) {
        return out;
    }

    const float clamped_voxel_size = std::max(voxel_size_meters, 0.05f);
    const float voxel_volume = clamped_voxel_size * clamped_voxel_size * clamped_voxel_size;

    glm::vec3 weighted_sum(0.0f);
    glm::vec3 min_cell = glm::vec3(cell_center(cells.front().cell, clamped_voxel_size));
    glm::vec3 max_cell = min_cell;

    for (const VoxelMassCell &cell : cells) {
        const float density = std::max(cell.material.density_kg_per_m3, 1.0f);
        const float cell_mass = density * voxel_volume;
        const glm::vec3 center = cell_center(cell.cell, clamped_voxel_size);
        weighted_sum += center * cell_mass;
        out.mass.total_mass_kg += cell_mass;

        min_cell.x = std::min(min_cell.x, center.x - clamped_voxel_size * 0.5f);
        min_cell.y = std::min(min_cell.y, center.y - clamped_voxel_size * 0.5f);
        min_cell.z = std::min(min_cell.z, center.z - clamped_voxel_size * 0.5f);
        max_cell.x = std::max(max_cell.x, center.x + clamped_voxel_size * 0.5f);
        max_cell.y = std::max(max_cell.y, center.y + clamped_voxel_size * 0.5f);
        max_cell.z = std::max(max_cell.z, center.z + clamped_voxel_size * 0.5f);
    }

    if (out.mass.total_mass_kg <= 0.0f) {
        return out;
    }
    out.mass.center_of_mass = weighted_sum / out.mass.total_mass_kg;
    out.mass.local_bounds_min = min_cell;
    out.mass.local_bounds_max = max_cell;

    // Parallel axis theorem against point masses at voxel centers.
    for (const VoxelMassCell &cell : cells) {
        const float density = std::max(cell.material.density_kg_per_m3, 1.0f);
        const float cell_mass = density * voxel_volume;
        const glm::vec3 r = cell_center(cell.cell, clamped_voxel_size) - out.mass.center_of_mass;
        out.mass.inertia_diagonal.x += cell_mass * (r.y * r.y + r.z * r.z);
        out.mass.inertia_diagonal.y += cell_mass * (r.x * r.x + r.z * r.z);
        out.mass.inertia_diagonal.z += cell_mass * (r.x * r.x + r.y * r.y);
    }

    const glm::vec3 bmin = out.mass.local_bounds_min;
    const glm::vec3 bmax = out.mass.local_bounds_max;
    const float wheel_y = bmin.y + 0.02f;
    out.wheel_mounts.push_back({glm::vec3(bmin.x + 0.2f, wheel_y, bmin.z + 0.2f)});
    out.wheel_mounts.push_back({glm::vec3(bmax.x - 0.2f, wheel_y, bmin.z + 0.2f)});
    out.wheel_mounts.push_back({glm::vec3(bmin.x + 0.2f, wheel_y, bmax.z - 0.2f)});
    out.wheel_mounts.push_back({glm::vec3(bmax.x - 0.2f, wheel_y, bmax.z - 0.2f)});

    return out;
}
