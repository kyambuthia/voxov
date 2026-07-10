#include "engine_physics/flight_vehicle.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

void FlightVehicle::init(const VehicleConfig& config) {
    config_ = config;
    state_ = VehicleState{};
    forces_ = AeroForces{};
    current_air_density_ = 0.0;
    state_.fuel_remaining = config_.fuel_capacity;
}

// ── Aerodynamic forces ──────────────────────────────────────────────────
// WHY: Real aerodynamic forces (lift, drag) depend on air density (exponential
// decay with altitude), squared velocity, wing area, and coefficients.
// In space (air_density ≈ 0), lift and drag become zero — only thrust
// and gravity remain for orbital mechanics.
//
// Equations (standard aerodynamics):
//   dynamic_pressure = 0.5 * air_density * v²
//   Thrust = throttle * max_thrust * forward    (always in forward direction)
//   Lift   = dynamic_pressure * wing_area * C_L * lift_dir
//            where lift_dir is perpendicular to velocity in the up-plane
//   Drag   = dynamic_pressure * wing_area * C_D * (-velocity_dir)
//   Gravity = mass * gravity_vector

void FlightVehicle::compute_aero_forces(double air_density) {
    current_air_density_ = air_density;
    forces_ = AeroForces{};

    const double speed = glm::length(state_.velocity);
    // Prevent division by zero when stationary.
    const glm::dvec3 vel_dir = (speed > 1e-6)
        ? state_.velocity / speed
        : state_.forward;  // fallback when stationary

    const double dyn_pressure = 0.5 * air_density * speed * speed;

    // ── Thrust ──────────────────────────────────────────────────────────
    // Engine thrust only when active and with throttle > 0.
    if (state_.engine_active && state_.throttle > 0.0) {
        forces_.thrust = state_.forward * (state_.throttle * config_.max_thrust);
    }

    // ── Lift ────────────────────────────────────────────────────────────
    // Lift acts perpendicular to velocity in the plane defined by velocity
    // and the vehicle's "up" vector.  Uses cross products:
    //   lift_dir = (vel_dir × right) normalized
    // This gives a direction perpendicular to velocity that tilts toward
    // the vehicle's up direction — standard wing lift model.
    if (air_density > 1e-8 && speed > 0.1) {
        const glm::dvec3 horiz_in_vel_plane = glm::normalize(
            glm::cross(vel_dir, state_.right));
        // Angle of attack: how much the wings are tilted relative to velocity.
        // When aoa < 0 the nose is below velocity (diving), reducing lift.
        const double aoa = std::asin(std::clamp(
            glm::dot(state_.forward, vel_dir), -1.0, 1.0));
        // Lift coefficient scales with angle of attack (simplified linear model
        // with stall drop-off beyond ~15°, typical for subsonic flight).
        const double aoa_deg = std::abs(aoa * 180.0 / 3.141592653589793);
        double cl_effective = config_.lift_coefficient;
        if (aoa_deg > 15.0) {
            // Stall: lift drops dramatically beyond critical AoA.
            cl_effective *= std::max(0.1, 1.0 - (aoa_deg - 15.0) * 0.15);
        }
        forces_.lift = horiz_in_vel_plane *
            (dyn_pressure * config_.wing_area * cl_effective);
    }

    // ── Drag ────────────────────────────────────────────────────────────
    // Drag opposes velocity.  Increases with angle of attack (induced drag).
    if (air_density > 1e-8 && speed > 0.1) {
        const double aoa = std::asin(std::clamp(
            glm::dot(state_.forward, vel_dir), -1.0, 1.0));
        // Induced drag increases with AoA² (proportional to lift²).
        const double induced_factor = 1.0 + std::abs(aoa) * 2.5;
        forces_.drag = -vel_dir *
            (dyn_pressure * config_.wing_area * config_.drag_coefficient * induced_factor);
    }
}

// ── Semi-implicit Euler integration ─────────────────────────────────────
// WHY semi-implicit (symplectic) Euler: conserves energy better than
// explicit Euler for orbital/atmospheric flight.  Velocity is updated
// first, then position uses the new velocity.
//
// Steps:
//   1. Compute net force: thrust + lift + drag + gravity
//   2. a = F / mass
//   3. v(t+dt) = v(t) + a * dt
//   4. p(t+dt) = p(t) + v(t+dt) * dt
//   5. Consume fuel: fuel -= throttle * consumption_rate * dt

void FlightVehicle::update(double dt, double air_density, const glm::dvec3& gravity) {
    if (dt <= 0.0) return;

    compute_aero_forces(air_density);

    // Gravity force.
    forces_.gravity = config_.mass * gravity;

    // Total force.
    forces_.total = forces_.thrust + forces_.lift + forces_.drag + forces_.gravity;

    // Acceleration.
    const glm::dvec3 acceleration = forces_.total / config_.mass;

    // Semi-implicit Euler: update velocity first, then position.
    state_.velocity += acceleration * dt;
    state_.position += state_.velocity * dt;

    // Fuel consumption: only when engine is active and producing thrust.
    if (state_.engine_active && state_.throttle > 0.0) {
        state_.fuel_remaining -= state_.throttle * config_.fuel_consumption * dt;
        state_.fuel_remaining = std::max(0.0, state_.fuel_remaining);
        // Engine shuts off when out of fuel.
        if (state_.fuel_remaining <= 0.0) {
            state_.engine_active = false;
            state_.throttle = 0.0;
        }
    }

    // Apply rotation from control inputs, then update derived vectors.
    apply_rotation(dt);
    update_orientation_vectors();
}

// ── Rotation from pitch/yaw/roll rates ──────────────────────────────────
// Pitch: rotate around right axis
// Yaw:   rotate around up axis
// Roll:  rotate around forward axis
// WHY quaternion multiplication order: local-axis rotations are applied
// as incremental quaternions multiplied on the right (post-multiply),
// so they act in the vehicle's local body frame.

void FlightVehicle::apply_rotation(double dt) {
    if (pitch_rate_ == 0.0 && yaw_rate_ == 0.0 && roll_rate_ == 0.0) return;
    if (dt <= 0.0) return;

    // Build incremental rotation quaternion for this timestep.
    // Order: pitch (right), then yaw (up), then roll (forward).
    const glm::dquat q_pitch = glm::angleAxis(pitch_rate_ * dt, state_.right);
    const glm::dquat q_yaw   = glm::angleAxis(yaw_rate_ * dt, state_.up);
    const glm::dquat q_roll  = glm::angleAxis(roll_rate_ * dt, state_.forward);

    // Post-multiply to rotate in local body frame.
    state_.orientation = glm::normalize(
        state_.orientation * q_pitch * q_yaw * q_roll);
}

// Control inputs — accumulate angular rates (applied in apply_rotation).
void FlightVehicle::set_throttle(double throttle) {
    state_.throttle = std::clamp(throttle, 0.0, 1.0);
}

void FlightVehicle::apply_pitch(double rate) {
    pitch_rate_ = std::clamp(rate, -5.0, 5.0);
}

void FlightVehicle::apply_yaw(double rate) {
    yaw_rate_ = std::clamp(rate, -5.0, 5.0);
}

void FlightVehicle::apply_roll(double rate) {
    roll_rate_ = std::clamp(rate, -5.0, 5.0);
}

// ── Orientation vectors ─────────────────────────────────────────────────
// Extracts forward (local +Z), up (local +Y), and right (local +X) from
// the quaternion.  These are used for thrust direction, lift computation,
// and camera orientation.

void FlightVehicle::update_orientation_vectors() {
    const glm::dmat3 rot = glm::mat3_cast(state_.orientation);
    // Column 0 = right (+X), Column 1 = up (+Y), Column 2 = forward (+Z)
    state_.right   = glm::dvec3(rot[0]);
    state_.up      = glm::dvec3(rot[1]);
    state_.forward = glm::dvec3(rot[2]);
}
