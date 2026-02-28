#include "engine_physics/vehicle/vehicle_damage_model.hpp"

#include <algorithm>
#include <cmath>

void VehicleDamageModel::initialize(const std::vector<VoxelMassCell> &cells, float base_hit_points) {
    damage_cells.clear();
    damage_cells.reserve(cells.size());
    const float hp = std::max(base_hit_points, 1.0f);
    for (const VoxelMassCell &cell : cells) {
        VoxelDamageCell d{};
        d.cell = cell.cell;
        d.hit_points = hp;
        d.destroyed = false;
        damage_cells.push_back(d);
    }
    recalculate_stats();
}

void VehicleDamageModel::apply_impact(const glm::vec3 &local_impact_point, float energy_joules, float radius_meters) {
    if (damage_cells.empty() || energy_joules <= 0.0f) {
        return;
    }

    const float radius = std::max(radius_meters, 0.1f);
    for (VoxelDamageCell &cell : damage_cells) {
        if (cell.destroyed) {
            continue;
        }
        const glm::vec3 center = glm::vec3(cell.cell) + glm::vec3(0.5f);
        const float d = glm::length(center - local_impact_point);
        if (d > radius) {
            continue;
        }

        const float falloff = 1.0f - (d / radius);
        const float damage = energy_joules * falloff * 0.045f;
        cell.hit_points -= damage;
        if (cell.hit_points <= 0.0f) {
            cell.hit_points = 0.0f;
            cell.destroyed = true;
        }
    }
    recalculate_stats();
}

void VehicleDamageModel::repair_all() {
    for (VoxelDamageCell &cell : damage_cells) {
        cell.destroyed = false;
        cell.hit_points = std::max(cell.hit_points, 1.0f);
    }
    recalculate_stats();
}

const std::vector<VoxelDamageCell> &VehicleDamageModel::cells() const {
    return damage_cells;
}

const VoxelDamageStats &VehicleDamageModel::stats() const {
    return current;
}

void VehicleDamageModel::recalculate_stats() {
    current = VoxelDamageStats{};
    if (damage_cells.empty()) {
        return;
    }
    uint32_t alive = 0;
    uint32_t destroyed = 0;
    for (const VoxelDamageCell &cell : damage_cells) {
        if (cell.destroyed) {
            ++destroyed;
        } else {
            ++alive;
        }
    }
    current.destroyed_cells = destroyed;
    current.integrity = static_cast<float>(alive) / static_cast<float>(damage_cells.size());
    current.mass_scale = std::clamp(current.integrity, 0.25f, 1.0f);
}
