#include "engine_physics/vehicle/ground_vehicle_controller.hpp"

#include "engine_world/physics/voxel_collision.hpp"

#include <algorithm>
#include <cmath>

namespace {
glm::vec3 rotate_y(const glm::vec3 &v, float yaw_radians) {
    const float c = std::cos(yaw_radians);
    const float s = std::sin(yaw_radians);
    return glm::vec3(v.x * c - v.z * s, v.y, v.x * s + v.z * c);
}
}

GroundVehicleController::GroundVehicleController() {
    wheels = {{
        {glm::vec3(-0.85f, 0.0f, 1.20f), 0.32f, 0.38f, 0.24f},
        {glm::vec3(0.85f, 0.0f, 1.20f), 0.32f, 0.38f, 0.24f},
        {glm::vec3(-0.85f, 0.0f, -1.20f), 0.32f, 0.38f, 0.24f},
        {glm::vec3(0.85f, 0.0f, -1.20f), 0.32f, 0.38f, 0.24f},
    }};
}

void GroundVehicleController::set_tuning(const VehiclePhysicsTuning &new_tuning) {
    tuning = sanitize_vehicle_tuning(new_tuning);
}

void GroundVehicleController::set_wheels(const std::array<GroundVehicleWheel, 4> &new_wheels) {
    wheels = new_wheels;
}

void GroundVehicleController::reset(const glm::vec3 &position, float yaw_radians) {
    current = GroundVehicleState{};
    current.kinematic.position = position;
    current.kinematic.yaw = yaw_radians;
    drivetrain.reset();
    prev_vertical_error = 0.0f;
}

void GroundVehicleController::step(
    const VehicleControlInput &input,
    const VoxelCollisionWorld &collision_world,
    float dt_seconds) {
    const glm::vec3 fwd_before = glm::vec3(std::sin(current.kinematic.yaw), 0.0f, std::cos(current.kinematic.yaw));
    const float wheel_speed = glm::dot(current.kinematic.velocity, fwd_before);
    VehicleControlInput drive_input = input;
    drive_input.throttle = drivetrain.update(input.throttle, wheel_speed, dt_seconds);
    integrate_vehicle_kinematics(current.kinematic, drive_input, tuning, dt_seconds);

    float desired_chassis_y = 0.0f;
    uint32_t grounded_count = 0;
    std::array<float, 4> compression{};
    for (size_t i = 0; i < wheels.size(); ++i) {
        const GroundVehicleWheel &wheel = wheels[i];
        const glm::vec3 world_mount = current.kinematic.position + rotate_y(wheel.local_mount, current.kinematic.yaw);
        const glm::vec3 ray_origin = world_mount + glm::vec3(0.0f, wheel.suspension_rest_length + wheel.suspension_travel + 1.5f, 0.0f);
        const float ray_range = wheel.suspension_rest_length + wheel.suspension_travel + 3.0f;
        float hit_dist = 0.0f;
        bool have_hit = collision_world.raycast(ray_origin, glm::vec3(0.0f, -1.0f, 0.0f), ray_range, hit_dist);
        if (!have_hit) {
            have_hit = collision_world.raycast(ray_origin + glm::vec3(0.25f, 0.0f, 0.25f), glm::vec3(0.0f, -1.0f, 0.0f), ray_range, hit_dist);
        }
        const float ground_y = have_hit ? (ray_origin.y - hit_dist) : (world_mount.y - wheel.suspension_rest_length - wheel.radius);
        const float wheel_bottom_y = world_mount.y - wheel.radius;
        const float suspension_length = std::clamp(
            world_mount.y - ground_y - wheel.radius,
            0.0f,
            wheel.suspension_rest_length + wheel.suspension_travel);
        const float c = std::clamp(
            (wheel.suspension_rest_length - suspension_length) / std::max(wheel.suspension_rest_length, 0.05f),
            0.0f,
            1.0f);
        compression[i] = c;

        if (have_hit && wheel_bottom_y <= (ground_y + wheel.suspension_travel + 0.05f)) {
            ++grounded_count;
            desired_chassis_y += ground_y + wheel.radius + wheel.suspension_rest_length;
        }
    }

    current.wheel_compression = compression;
    if (grounded_count > 0) {
        desired_chassis_y /= static_cast<float>(grounded_count);
        const float error = desired_chassis_y - current.kinematic.position.y;
        const float spring_force = error * (tuning.suspension_stiffness / std::max(tuning.mass_kg, 1.0f));
        const float damping_force = ((error - prev_vertical_error) / std::max(dt_seconds, 1.0e-4f)) *
            (tuning.suspension_damping / std::max(tuning.mass_kg, 1.0f));
        current.kinematic.velocity.y += (spring_force + damping_force) * dt_seconds;
        current.kinematic.position.y += current.kinematic.velocity.y * dt_seconds;
        prev_vertical_error = error;
    } else {
        current.kinematic.velocity.y += -9.81f * dt_seconds;
        current.kinematic.position.y += current.kinematic.velocity.y * dt_seconds;
    }

    const glm::vec3 fwd = glm::vec3(std::sin(current.kinematic.yaw), 0.0f, std::cos(current.kinematic.yaw));
    const float speed = glm::dot(current.kinematic.velocity, fwd);
    current.telemetry.speed_mps = std::fabs(speed);
    current.telemetry.engine_load = std::clamp(
        drivetrain.telemetry().engine_rpm / 7000.0f,
        0.0f,
        1.0f);
    current.telemetry.longitudinal_slip = std::clamp(input.throttle - speed * 0.06f, -1.0f, 1.0f);
    current.telemetry.lateral_slip = std::clamp(std::fabs(input.steer) * current.telemetry.speed_mps * 0.08f, 0.0f, 1.0f);
    current.telemetry.grounded_wheels = grounded_count;
}

const GroundVehicleState &GroundVehicleController::state() const {
    return current;
}
