#pragma once

#include "engine_physics/vehicle/vehicle_foundation.hpp"

#include <array>

#include <glm/vec3.hpp>

class VoxelCollisionWorld;

struct GroundVehicleWheel {
    glm::vec3 local_mount = glm::vec3(0.0f);
    float radius = 0.32f;
    float suspension_rest_length = 0.40f;
    float suspension_travel = 0.24f;
};

struct GroundVehicleState {
    VehicleKinematicState kinematic{};
    VehiclePhysicsTelemetry telemetry{};
    std::array<float, 4> wheel_compression = {0.0f, 0.0f, 0.0f, 0.0f};
};

class GroundVehicleController {
public:
    GroundVehicleController();

    void set_tuning(const VehiclePhysicsTuning &new_tuning);
    void set_wheels(const std::array<GroundVehicleWheel, 4> &new_wheels);
    void reset(const glm::vec3 &position, float yaw_radians);

    void step(
        const VehicleControlInput &input,
        const VoxelCollisionWorld &collision_world,
        float dt_seconds);

    const GroundVehicleState &state() const;

private:
    VehiclePhysicsTuning tuning{};
    std::array<GroundVehicleWheel, 4> wheels{};
    GroundVehicleState current{};
    float prev_vertical_error = 0.0f;
};
