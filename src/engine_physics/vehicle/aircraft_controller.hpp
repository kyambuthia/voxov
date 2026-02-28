#pragma once

#include "engine_physics/vehicle/vehicle_foundation.hpp"

#include <glm/vec3.hpp>

class VoxelCollisionWorld;

struct AircraftSimState {
    AircraftKinematicState kinematic{};
    AircraftPhysicsTelemetry telemetry{};
};

class AircraftController {
public:
    void set_tuning(const AircraftPhysicsTuning &new_tuning);
    void reset(const glm::vec3 &position, const glm::vec3 &euler_radians, const glm::vec3 &velocity);

    void step(
        const AircraftControlInput &input,
        const VoxelCollisionWorld &collision_world,
        float dt_seconds);

    const AircraftSimState &state() const;

private:
    AircraftPhysicsTuning tuning{};
    AircraftSimState current{};
};
