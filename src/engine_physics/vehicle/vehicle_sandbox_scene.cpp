#include "engine_physics/vehicle/vehicle_sandbox_scene.hpp"

#include <algorithm>

void VehicleSandboxScene::init_default() {
    world_chunk.generate_flat_ground(0);
    for (int x = 6; x < 12; ++x) {
        world_chunk.set_solid(x, 1, 10, true);
    }
    voxel_bridge.clear();
    voxel_bridge.register_chunk(VoxelChunkCoord{0, 0}, world_chunk);

    vehicle.reset(glm::vec3(7.0f, 1.5f, 7.0f), 0.0f);
    aircraft.reset(glm::vec3(10.0f, 7.0f, 8.0f), glm::vec3(0.0f, 0.2f, 0.0f), glm::vec3(0.0f, 0.0f, 8.0f));

    std::vector<VoxelMassCell> damage_cells;
    damage_cells.reserve(2 * 2 * 3);
    for (int x = 0; x < 3; ++x) {
        for (int y = 0; y < 2; ++y) {
            for (int z = 0; z < 2; ++z) {
                damage_cells.push_back({glm::ivec3(x, y, z), VoxelMassMaterial{}});
            }
        }
    }
    vehicle_damage.initialize(damage_cells, 100.0f);
    current = VehicleSandboxSnapshot{};
}

void VehicleSandboxScene::step(const InputState &vehicle_input, const InputState &aircraft_input, float dt_seconds) {
    VehicleControlInput v{};
    v.throttle = vehicle_input.move.y;
    v.steer = vehicle_input.move.x;
    v.brake = vehicle_input.move.y < 0.0f ? std::min(1.0f, -vehicle_input.move.y) : 0.0f;
    v.handbrake = vehicle_input.crouch_held ? 1.0f : 0.0f;
    vehicle.step(v, collision_world, dt_seconds);

    AircraftControlInput a{};
    a.throttle = std::clamp(0.55f + aircraft_input.move.y * 0.45f, 0.0f, 1.0f);
    a.yaw = aircraft_input.move.x;
    a.pitch = (aircraft_input.jump_held ? 0.45f : 0.0f) + (aircraft_input.crouch_held ? -0.45f : 0.0f);
    a.roll = -aircraft_input.move.x * 0.5f;
    aircraft.step(a, collision_world, dt_seconds);

    if (vehicle_input.jump_pressed) {
        const glm::vec3 impact_point = vehicle.state().kinematic.position + glm::vec3(0.0f, 0.5f, 0.0f);
        vehicle_damage.apply_impact(impact_point, 1800.0f, 1.4f);
    }

    current.vehicle_position = vehicle.state().kinematic.position;
    current.aircraft_position = aircraft.state().kinematic.position;
    current.vehicle_speed_mps = vehicle.state().telemetry.speed_mps;
    current.aircraft_speed_mps = aircraft.state().telemetry.speed_mps;
    current.static_shape_count = static_cast<uint32_t>(voxel_bridge.build_all_shapes().size());
}

const VehicleSandboxSnapshot &VehicleSandboxScene::snapshot() const {
    return current;
}
