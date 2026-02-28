#include "engine_physics/vehicle/aircraft_controller.hpp"

#include "engine_world/physics/voxel_collision.hpp"

#include <algorithm>
#include <cmath>

void AircraftController::set_tuning(const AircraftPhysicsTuning &new_tuning) {
    tuning = sanitize_aircraft_tuning(new_tuning);
}

void AircraftController::reset(const glm::vec3 &position, const glm::vec3 &euler_radians, const glm::vec3 &velocity) {
    current = AircraftSimState{};
    current.kinematic.position = position;
    current.kinematic.euler = euler_radians;
    current.kinematic.velocity = velocity;
}

void AircraftController::step(
    const AircraftControlInput &input,
    const VoxelCollisionWorld &collision_world,
    float dt_seconds) {
    const AircraftControlInput control = sanitize_aircraft_input(input);
    integrate_aircraft_kinematics(current.kinematic, control, tuning, dt_seconds);

    const float speed = std::max(glm::length(current.kinematic.velocity), 0.1f);
    const float speed_ref = std::max(12.0f, speed);
    const float aoa = std::clamp(current.kinematic.euler.x, -0.75f, 0.75f);
    const float stall_factor = std::clamp(speed / speed_ref, 0.0f, 1.0f);

    const float lift = tuning.lift_coefficient * speed * speed * std::cos(aoa) * stall_factor;
    const float drag = tuning.drag_coefficient * speed * speed * (1.0f + std::fabs(aoa) * 0.35f);
    const glm::vec3 fwd(std::sin(current.kinematic.euler.y), -std::sin(current.kinematic.euler.x), std::cos(current.kinematic.euler.y));

    current.kinematic.velocity += glm::vec3(0.0f, (lift / std::max(tuning.mass_kg, 1.0f)) * dt_seconds, 0.0f);
    current.kinematic.velocity -= (current.kinematic.velocity / speed) * (drag / std::max(tuning.mass_kg, 1.0f)) * dt_seconds;
    current.kinematic.position += current.kinematic.velocity * dt_seconds;

    const float terrain_floor = collision_world.find_spawn_height(
        glm::vec2(current.kinematic.position.x, current.kinematic.position.z), 1.0f, 1.8f) + 2.2f;
    if (current.kinematic.position.y < terrain_floor) {
        current.kinematic.position.y = terrain_floor;
        current.kinematic.velocity.y = std::max(0.0f, current.kinematic.velocity.y);
    }

    current.telemetry.speed_mps = speed;
    current.telemetry.angle_of_attack_deg = aoa * (180.0f / 3.14159265f);
    current.telemetry.lift_newtons = lift;
    current.telemetry.drag_newtons = drag;
    current.telemetry.throttle = control.throttle;

    current.kinematic.euler.y = std::atan2(fwd.x, std::max(0.0001f, fwd.z));
}

const AircraftSimState &AircraftController::state() const {
    return current;
}
