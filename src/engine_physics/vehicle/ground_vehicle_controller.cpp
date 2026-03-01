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
        {glm::vec3(-0.85f, 0.38f, 1.20f), 0.32f, 0.38f, 0.24f},
        {glm::vec3(0.85f, 0.38f, 1.20f), 0.32f, 0.38f, 0.24f},
        {glm::vec3(-0.85f, 0.38f, -1.20f), 0.32f, 0.38f, 0.24f},
        {glm::vec3(0.85f, 0.38f, -1.20f), 0.32f, 0.38f, 0.24f},
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
    if (dt_seconds <= 0.0f) {
        return;
    }
    const glm::vec3 fwd_before = glm::vec3(std::sin(current.kinematic.yaw), 0.0f, std::cos(current.kinematic.yaw));
    const float wheel_speed = glm::dot(current.kinematic.velocity, fwd_before);
    VehicleControlInput drive_input = input;
    drive_input.throttle = drivetrain.update(input.throttle, wheel_speed, dt_seconds);

    float desired_chassis_y = 0.0f;
    uint32_t grounded_count = 0;
    std::array<float, 4> compression{};
    float avg_ground_height = current.kinematic.position.y;
    for (size_t i = 0; i < wheels.size(); ++i) {
        const GroundVehicleWheel &wheel = wheels[i];
        const glm::vec3 world_mount = current.kinematic.position + rotate_y(wheel.local_mount, current.kinematic.yaw);
        const glm::vec3 ray_origin = world_mount + glm::vec3(0.0f, 0.2f, 0.0f);
        const float ray_range = wheel.suspension_rest_length + wheel.suspension_travel + wheel.radius + 0.5f;
        float hit_dist = 0.0f;
        bool have_hit = collision_world.raycast(ray_origin, glm::vec3(0.0f, -1.0f, 0.0f), ray_range, hit_dist);
        const float ground_y = have_hit ? (ray_origin.y - hit_dist) : (world_mount.y - wheel.suspension_rest_length - wheel.radius);
        const float suspension_length = std::clamp(have_hit ? (hit_dist - wheel.radius) : (wheel.suspension_rest_length + wheel.suspension_travel),
            0.0f,
            wheel.suspension_rest_length + wheel.suspension_travel);
        const float c = std::clamp(
            (wheel.suspension_rest_length - suspension_length) / std::max(wheel.suspension_travel, 0.05f),
            0.0f,
            1.0f);
        compression[i] = c;

        if (have_hit) {
            ++grounded_count;
            desired_chassis_y += ground_y + wheel.radius + wheel.suspension_rest_length - wheel.local_mount.y;
            avg_ground_height += ground_y;
        }
    }

    current.wheel_compression = compression;
    if (grounded_count > 0) {
        desired_chassis_y /= static_cast<float>(grounded_count);
        avg_ground_height /= static_cast<float>(grounded_count + 1u);
        const float error = desired_chassis_y - current.kinematic.position.y;
        const float spring_force = error * (tuning.suspension_stiffness / std::max(tuning.mass_kg, 1.0f));
        const float damping_force = ((error - prev_vertical_error) / std::max(dt_seconds, 1.0e-4f)) *
            (tuning.suspension_damping / std::max(tuning.mass_kg, 1.0f));
        current.kinematic.velocity.y += (spring_force + damping_force) * dt_seconds;
        current.kinematic.position.y += current.kinematic.velocity.y * dt_seconds;
        prev_vertical_error = error;
        if (current.kinematic.position.y < (avg_ground_height + 0.02f)) {
            current.kinematic.position.y = avg_ground_height + 0.02f;
            current.kinematic.velocity.y = std::max(0.0f, current.kinematic.velocity.y);
        }
    } else {
        current.kinematic.velocity.y += -9.81f * dt_seconds;
        current.kinematic.position.y += current.kinematic.velocity.y * dt_seconds;
    }

    const glm::vec3 fwd = glm::vec3(std::sin(current.kinematic.yaw), 0.0f, std::cos(current.kinematic.yaw));
    const glm::vec3 right = glm::vec3(fwd.z, 0.0f, -fwd.x);
    float speed = glm::dot(current.kinematic.velocity, fwd);
    float lateral_speed = glm::dot(current.kinematic.velocity, right);
    const float grounded_scale = std::clamp(static_cast<float>(grounded_count) / 4.0f, 0.2f, 1.0f);

    const float drive_force = drive_input.throttle * tuning.max_drive_force * grounded_scale;
    const float brake_force = drive_input.brake * tuning.max_brake_force;
    const float rolling = (std::fabs(drive_input.throttle) < 0.05f ? 280.0f : 110.0f) * ((speed >= 0.0f) ? 1.0f : -1.0f);
    const float drag = 0.38f * speed * std::fabs(speed);
    const float brake_term = (speed != 0.0f) ? (brake_force * ((speed >= 0.0f) ? 1.0f : -1.0f)) : 0.0f;
    const float accel = (drive_force - brake_term - rolling - drag) / std::max(tuning.mass_kg, 1.0f);
    speed += accel * dt_seconds;

    const float front_axle_z = 0.5f * (wheels[0].local_mount.z + wheels[1].local_mount.z);
    const float rear_axle_z = 0.5f * (wheels[2].local_mount.z + wheels[3].local_mount.z);
    const float wheel_base = std::max(std::fabs(front_axle_z - rear_axle_z), 1.2f);
    const float steer_angle = drive_input.steer * tuning.max_steer_radians * std::clamp(0.2f + std::fabs(speed) * 0.07f, 0.2f, 1.0f);
    current.kinematic.yaw_rate = (speed / wheel_base) * std::tan(steer_angle) * grounded_scale;
    current.kinematic.yaw += current.kinematic.yaw_rate * dt_seconds;

    lateral_speed *= std::clamp(1.0f - (tuning.tire_grip * (1.0f + drive_input.handbrake) * dt_seconds), 0.0f, 1.0f);
    const glm::vec3 new_fwd = glm::vec3(std::sin(current.kinematic.yaw), 0.0f, std::cos(current.kinematic.yaw));
    const glm::vec3 new_right = glm::vec3(new_fwd.z, 0.0f, -new_fwd.x);
    current.kinematic.velocity.x = new_fwd.x * speed + new_right.x * lateral_speed;
    current.kinematic.velocity.z = new_fwd.z * speed + new_right.z * lateral_speed;
    current.kinematic.position += glm::vec3(current.kinematic.velocity.x, 0.0f, current.kinematic.velocity.z) * dt_seconds;

    current.telemetry.speed_mps = std::fabs(speed);
    current.telemetry.engine_load = std::clamp(
        drivetrain.telemetry().engine_rpm / 7000.0f,
        0.0f,
        1.0f);
    current.telemetry.longitudinal_slip = std::clamp(drive_input.throttle - speed * 0.045f, -1.0f, 1.0f);
    current.telemetry.lateral_slip = std::clamp(std::fabs(lateral_speed) * 0.22f, 0.0f, 1.0f);
    current.telemetry.grounded_wheels = grounded_count;
}

const GroundVehicleState &GroundVehicleController::state() const {
    return current;
}

const std::array<GroundVehicleWheel, 4> &GroundVehicleController::wheel_setup() const {
    return wheels;
}
