#pragma once

#include "engine_input/input_state.hpp"
#include "engine_physics/vehicle/aircraft_controller.hpp"
#include "engine_physics/vehicle/ground_vehicle_controller.hpp"
#include "engine_physics/vehicle/vehicle_damage_model.hpp"
#include "engine_physics/voxel/voxel_physics_bridge.hpp"
#include "engine_world/physics/voxel_collision.hpp"
#include "engine_world/voxel_chunk.hpp"

struct VehicleSandboxSnapshot {
    glm::vec3 vehicle_position = glm::vec3(0.0f);
    glm::vec3 aircraft_position = glm::vec3(0.0f);
    float vehicle_speed_mps = 0.0f;
    float aircraft_speed_mps = 0.0f;
    uint32_t static_shape_count = 0;
};

class VehicleSandboxScene {
public:
    void init_default();
    void step(const InputState &vehicle_input, const InputState &aircraft_input, float dt_seconds);

    const VehicleSandboxSnapshot &snapshot() const;

private:
    VoxelChunk world_chunk{};
    VoxelCollisionWorld collision_world{&world_chunk};
    VoxelPhysicsBridge voxel_bridge{};
    GroundVehicleController vehicle{};
    AircraftController aircraft{};
    VehicleDamageModel vehicle_damage{};
    VehicleSandboxSnapshot current{};
};
