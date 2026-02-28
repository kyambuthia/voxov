#include "engine_physics/vehicle/vehicle_foundation.hpp"

#include <algorithm>
#include <cmath>

#include <glm/geometric.hpp>

namespace {
float clamp_signed(float v) {
    return std::clamp(v, -1.0f, 1.0f);
}

float safe_mass(float m) {
    return std::max(m, 1.0f);
}

glm::vec3 forward_from_yaw(float yaw) {
    return glm::vec3(std::sin(yaw), 0.0f, std::cos(yaw));
}

float abs_or_min(float v, float min_v) {
    return std::max(std::fabs(v), min_v);
}
}

FixedStepCounter::FixedStepCounter(double fixed_dt_seconds)
    : dt(std::max(fixed_dt_seconds, 1.0 / 240.0)) {}

uint32_t FixedStepCounter::consume(double frame_dt_seconds) {
    accumulator += std::clamp(frame_dt_seconds, 0.0, 0.25);
    uint32_t steps = 0;
    while (accumulator >= dt && steps < 8) {
        accumulator -= dt;
        ++steps;
        ++tick_count;
    }
    if (steps == 8 && accumulator >= dt) {
        accumulator = std::fmod(accumulator, dt);
    }
    return steps;
}

double FixedStepCounter::alpha() const {
    return std::clamp(accumulator / dt, 0.0, 1.0);
}

double FixedStepCounter::fixed_dt() const {
    return dt;
}

uint64_t FixedStepCounter::tick() const {
    return tick_count;
}

VehicleControlInput sanitize_vehicle_input(const VehicleControlInput &in) {
    VehicleControlInput out{};
    out.throttle = clamp_signed(in.throttle);
    out.brake = std::clamp(in.brake, 0.0f, 1.0f);
    out.steer = clamp_signed(in.steer);
    out.handbrake = std::clamp(in.handbrake, 0.0f, 1.0f);
    return out;
}

AircraftControlInput sanitize_aircraft_input(const AircraftControlInput &in) {
    AircraftControlInput out{};
    out.throttle = std::clamp(in.throttle, 0.0f, 1.0f);
    out.pitch = clamp_signed(in.pitch);
    out.roll = clamp_signed(in.roll);
    out.yaw = clamp_signed(in.yaw);
    return out;
}

VehiclePhysicsTuning sanitize_vehicle_tuning(const VehiclePhysicsTuning &in) {
    VehiclePhysicsTuning out = in;
    out.mass_kg = safe_mass(in.mass_kg);
    out.max_drive_force = std::max(in.max_drive_force, 10.0f);
    out.max_brake_force = std::max(in.max_brake_force, 10.0f);
    out.max_steer_radians = std::clamp(in.max_steer_radians, 0.01f, 1.3f);
    out.tire_grip = std::clamp(in.tire_grip, 0.1f, 3.0f);
    out.suspension_stiffness = std::max(in.suspension_stiffness, 100.0f);
    out.suspension_damping = std::max(in.suspension_damping, 10.0f);
    return out;
}

AircraftPhysicsTuning sanitize_aircraft_tuning(const AircraftPhysicsTuning &in) {
    AircraftPhysicsTuning out = in;
    out.mass_kg = safe_mass(in.mass_kg);
    out.max_thrust_newtons = std::max(in.max_thrust_newtons, 100.0f);
    out.lift_coefficient = std::clamp(in.lift_coefficient, 0.01f, 8.0f);
    out.drag_coefficient = std::clamp(in.drag_coefficient, 0.001f, 2.0f);
    out.pitch_torque = std::max(in.pitch_torque, 10.0f);
    out.roll_torque = std::max(in.roll_torque, 10.0f);
    out.yaw_torque = std::max(in.yaw_torque, 10.0f);
    return out;
}

void integrate_vehicle_kinematics(
    VehicleKinematicState &state,
    const VehicleControlInput &control_in,
    const VehiclePhysicsTuning &tuning_in,
    float dt_seconds) {
    if (dt_seconds <= 0.0f) {
        return;
    }
    const VehicleControlInput control = sanitize_vehicle_input(control_in);
    const VehiclePhysicsTuning tuning = sanitize_vehicle_tuning(tuning_in);
    const glm::vec3 fwd = forward_from_yaw(state.yaw);
    const float speed = glm::dot(state.velocity, fwd);

    const float drive_force = control.throttle * tuning.max_drive_force;
    const float brake_force = control.brake * tuning.max_brake_force * ((speed >= 0.0f) ? 1.0f : -1.0f);
    const float drag_force = -0.42f * speed * abs_or_min(speed, 0.1f);
    const float net_force = drive_force - brake_force + drag_force;
    const float accel = net_force / tuning.mass_kg;
    const float next_speed = speed + accel * dt_seconds;

    state.yaw_rate = control.steer * tuning.max_steer_radians * std::clamp(std::fabs(next_speed) * 0.08f, 0.0f, 1.0f);
    state.yaw += state.yaw_rate * dt_seconds;
    const glm::vec3 new_fwd = forward_from_yaw(state.yaw);

    const float lateral_decay = std::clamp(1.0f - (tuning.tire_grip * dt_seconds * (1.0f + control.handbrake)), 0.0f, 1.0f);
    const glm::vec3 lateral = state.velocity - new_fwd * glm::dot(state.velocity, new_fwd);
    state.velocity = new_fwd * next_speed + lateral * lateral_decay;
    state.position += state.velocity * dt_seconds;
}

void integrate_aircraft_kinematics(
    AircraftKinematicState &state,
    const AircraftControlInput &control_in,
    const AircraftPhysicsTuning &tuning_in,
    float dt_seconds) {
    if (dt_seconds <= 0.0f) {
        return;
    }
    const AircraftControlInput control = sanitize_aircraft_input(control_in);
    const AircraftPhysicsTuning tuning = sanitize_aircraft_tuning(tuning_in);

    const float speed = std::max(glm::length(state.velocity), 0.1f);
    const glm::vec3 fwd(std::sin(state.euler.y), -std::sin(state.euler.x), std::cos(state.euler.y));
    const float thrust = control.throttle * tuning.max_thrust_newtons;
    const float lift = tuning.lift_coefficient * speed * speed;
    const float drag = tuning.drag_coefficient * speed * speed;

    const glm::vec3 force =
        fwd * thrust +
        glm::vec3(0.0f, lift, 0.0f) +
        glm::vec3(0.0f, -9.81f * tuning.mass_kg, 0.0f) -
        (state.velocity / speed) * drag;
    const glm::vec3 accel = force / tuning.mass_kg;
    state.velocity += accel * dt_seconds;
    state.position += state.velocity * dt_seconds;

    state.ang_vel.x += (control.pitch * tuning.pitch_torque / tuning.mass_kg) * dt_seconds;
    state.ang_vel.y += (control.yaw * tuning.yaw_torque / tuning.mass_kg) * dt_seconds;
    state.ang_vel.z += (control.roll * tuning.roll_torque / tuning.mass_kg) * dt_seconds;
    state.ang_vel *= std::clamp(1.0f - dt_seconds * 1.8f, 0.0f, 1.0f);
    state.euler += state.ang_vel * dt_seconds;
}
