#pragma once

#include <cstdint>

#include <glm/vec3.hpp>

struct VehicleControlInput {
    float throttle = 0.0f;   // [-1, 1]
    float brake = 0.0f;      // [0, 1]
    float steer = 0.0f;      // [-1, 1]
    float handbrake = 0.0f;  // [0, 1]
};

struct AircraftControlInput {
    float throttle = 0.0f;  // [0, 1]
    float pitch = 0.0f;     // [-1, 1]
    float roll = 0.0f;      // [-1, 1]
    float yaw = 0.0f;       // [-1, 1]
};

struct VehiclePhysicsTuning {
    float mass_kg = 1200.0f;
    float max_drive_force = 9200.0f;
    float max_brake_force = 11000.0f;
    float max_steer_radians = 0.52f;
    float tire_grip = 1.0f;
    float suspension_stiffness = 26000.0f;
    float suspension_damping = 3200.0f;
};

struct AircraftPhysicsTuning {
    float mass_kg = 1800.0f;
    float max_thrust_newtons = 42000.0f;
    float lift_coefficient = 0.85f;
    float drag_coefficient = 0.034f;
    float pitch_torque = 18000.0f;
    float roll_torque = 15000.0f;
    float yaw_torque = 12000.0f;
};

struct VehiclePhysicsTelemetry {
    float speed_mps = 0.0f;
    float longitudinal_slip = 0.0f;
    float lateral_slip = 0.0f;
    float engine_load = 0.0f;
    uint32_t grounded_wheels = 0;
};

struct AircraftPhysicsTelemetry {
    float speed_mps = 0.0f;
    float angle_of_attack_deg = 0.0f;
    float lift_newtons = 0.0f;
    float drag_newtons = 0.0f;
    float throttle = 0.0f;
};

struct VehicleKinematicState {
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 velocity = glm::vec3(0.0f);
    float yaw = 0.0f;
    float yaw_rate = 0.0f;
};

struct AircraftKinematicState {
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 velocity = glm::vec3(0.0f);
    glm::vec3 euler = glm::vec3(0.0f);      // pitch, yaw, roll
    glm::vec3 ang_vel = glm::vec3(0.0f);    // pitch, yaw, roll rates
};

class FixedStepCounter {
public:
    explicit FixedStepCounter(double fixed_dt_seconds = (1.0 / 60.0));

    uint32_t consume(double frame_dt_seconds);
    double alpha() const;
    double fixed_dt() const;
    uint64_t tick() const;

private:
    double dt = 1.0 / 60.0;
    double accumulator = 0.0;
    uint64_t tick_count = 0;
};

VehicleControlInput sanitize_vehicle_input(const VehicleControlInput &in);
AircraftControlInput sanitize_aircraft_input(const AircraftControlInput &in);
VehiclePhysicsTuning sanitize_vehicle_tuning(const VehiclePhysicsTuning &in);
AircraftPhysicsTuning sanitize_aircraft_tuning(const AircraftPhysicsTuning &in);

void integrate_vehicle_kinematics(
    VehicleKinematicState &state,
    const VehicleControlInput &control,
    const VehiclePhysicsTuning &tuning,
    float dt_seconds);

void integrate_aircraft_kinematics(
    AircraftKinematicState &state,
    const AircraftControlInput &control,
    const AircraftPhysicsTuning &tuning,
    float dt_seconds);
