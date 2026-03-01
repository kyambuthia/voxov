#include "engine_physics/vehicle/aircraft_controller.hpp"

#include "engine_world/physics/voxel_collision.hpp"

#include <algorithm>
#include <cmath>

#include <glm/geometric.hpp>

namespace {
constexpr float k_pi = 3.14159265358979323846f;

float wrap_angle(float radians) {
    while (radians > k_pi) {
        radians -= 2.0f * k_pi;
    }
    while (radians < -k_pi) {
        radians += 2.0f * k_pi;
    }
    return radians;
}
}

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
    if (dt_seconds <= 0.0f) {
        return;
    }
    const AircraftControlInput control = sanitize_aircraft_input(input);
    const AircraftPhysicsTuning stable_tuning = sanitize_aircraft_tuning(tuning);

    // Aircraft body rates (pitch/yaw/roll) with simple damping.
    current.kinematic.ang_vel.x += (control.pitch * stable_tuning.pitch_torque / stable_tuning.mass_kg) * dt_seconds;
    current.kinematic.ang_vel.y += (control.yaw * stable_tuning.yaw_torque / stable_tuning.mass_kg) * dt_seconds;
    current.kinematic.ang_vel.z += (control.roll * stable_tuning.roll_torque / stable_tuning.mass_kg) * dt_seconds;
    current.kinematic.ang_vel *= std::clamp(1.0f - dt_seconds * 2.1f, 0.0f, 1.0f);
    current.kinematic.euler += current.kinematic.ang_vel * dt_seconds;
    current.kinematic.euler.x = std::clamp(current.kinematic.euler.x, -1.1f, 1.1f);
    current.kinematic.euler.y = wrap_angle(current.kinematic.euler.y);
    current.kinematic.euler.z = std::clamp(current.kinematic.euler.z, -1.0f, 1.0f);

    const float pitch = current.kinematic.euler.x;
    const float yaw = current.kinematic.euler.y;
    const float roll = current.kinematic.euler.z;
    const float cp = std::cos(pitch);
    const float sp = std::sin(pitch);
    const float cy = std::cos(yaw);
    const float sy = std::sin(yaw);
    const float cr = std::cos(roll);
    const float sr = std::sin(roll);

    // Rotation basis columns for yaw-pitch-roll (Y-X-Z).
    const glm::vec3 forward = glm::normalize(glm::vec3(sy * cp, sp, cy * cp));
    const glm::vec3 right = glm::normalize(glm::vec3(cy * cr + sy * sp * sr, cp * sr, -sy * cr + cy * sp * sr));
    const glm::vec3 up = glm::normalize(glm::cross(right, forward));

    const float speed = std::max(glm::length(current.kinematic.velocity), 0.1f);
    const glm::vec3 vel_dir = current.kinematic.velocity / speed;
    const float aoa = std::asin(std::clamp(glm::dot(up, vel_dir), -1.0f, 1.0f));
    const float stall_speed = 12.0f;
    const float speed_factor = std::clamp(speed / stall_speed, 0.0f, 1.0f);
    const float aoa_penalty = std::clamp(1.0f - std::fabs(aoa) * 1.35f, 0.0f, 1.0f);
    const float lift_factor = speed_factor * aoa_penalty;

    const float thrust = control.throttle * stable_tuning.max_thrust_newtons;
    const float lift = stable_tuning.lift_coefficient * speed * speed * lift_factor;
    const float drag = stable_tuning.drag_coefficient * speed * speed * (1.0f + std::fabs(aoa) * 0.45f);
    const glm::vec3 force =
        forward * thrust +
        up * lift -
        vel_dir * drag +
        glm::vec3(0.0f, -9.81f * stable_tuning.mass_kg, 0.0f);
    const glm::vec3 accel = force / stable_tuning.mass_kg;
    current.kinematic.velocity += accel * dt_seconds;
    current.kinematic.position += current.kinematic.velocity * dt_seconds;

    const float terrain_floor = collision_world.find_spawn_height(
        glm::vec2(current.kinematic.position.x, current.kinematic.position.z), 1.0f, 1.8f) + 2.2f;
    if (current.kinematic.position.y < terrain_floor) {
        current.kinematic.position.y = terrain_floor;
        current.kinematic.velocity.y = std::max(0.0f, current.kinematic.velocity.y);
    }

    current.telemetry.speed_mps = glm::length(current.kinematic.velocity);
    current.telemetry.angle_of_attack_deg = aoa * (180.0f / k_pi);
    current.telemetry.lift_newtons = lift;
    current.telemetry.drag_newtons = drag;
    current.telemetry.throttle = control.throttle;
}

const AircraftSimState &AircraftController::state() const {
    return current;
}
