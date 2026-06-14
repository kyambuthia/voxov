#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

/// Vehicle configuration parameters.
/// WHY doubles: planet-scale positions require float64 range; radii up to 2000 km.
struct VehicleConfig {
    double mass = 1000.0;              // kg
    double wing_area = 20.0;           // m²
    double drag_coefficient = 0.02;    // dimensionless
    double lift_coefficient = 0.5;     // at optimal angle of attack
    double max_thrust = 50000.0;       // Newtons
    double fuel_capacity = 100.0;      // kg
    double fuel_consumption = 0.1;     // kg per Newton per second
};

/// Runtime vehicle state (doubles for planet-scale precision).
struct VehicleState {
    glm::dvec3 position{0.0};
    glm::dvec3 velocity{0.0};
    glm::dquat orientation{1.0, 0.0, 0.0, 0.0};
    double fuel_remaining = 100.0;
    double throttle = 0.0;             // 0-1
    bool engine_active = false;

    // Derived orientation vectors — updated from quaternion each timestep.
    glm::dvec3 forward{0.0, 0.0, 1.0};
    glm::dvec3 up{0.0, 1.0, 0.0};
    glm::dvec3 right{1.0, 0.0, 0.0};
};

/// Force breakdown for debug / HUD display.
struct AeroForces {
    glm::dvec3 thrust{0.0};
    glm::dvec3 lift{0.0};
    glm::dvec3 drag{0.0};
    glm::dvec3 gravity{0.0};
    glm::dvec3 total{0.0};
};

class FlightVehicle {
public:
    void init(const VehicleConfig& config);

    /// Update physics for one timestep.
    /// @param dt          timestep in seconds
    /// @param air_density kg/m³ — 0 in space, >0 in atmosphere
    /// @param gravity     gravitational acceleration vector (m/s² toward planet centre)
    void update(double dt, double air_density, const glm::dvec3& gravity);

    // Control inputs
    void set_throttle(double throttle);  // 0-1
    void apply_pitch(double rate);       // radians/sec
    void apply_yaw(double rate);         // radians/sec
    void apply_roll(double rate);        // radians/sec

    // State accessors (non-const for engine-side mutation during spawn / input)
    VehicleState& state() { return state_; }
    const VehicleState& state() const { return state_; }
    const AeroForces& forces() const { return forces_; }
    const VehicleConfig& config() const { return config_; }

    /// Returns true when the vehicle is in atmosphere (air_density > 1e-8).
    bool in_atmosphere() const { return current_air_density_ > 1e-8; }

private:
    VehicleConfig config_;
    VehicleState state_;
    AeroForces forces_;
    double current_air_density_ = 0.0;

    /// Compute aerodynamic forces for the current timestep.
    void compute_aero_forces(double air_density);

    /// Apply orientation change from pitch/yaw/roll rate accumulation.
    void apply_rotation(double dt);

    /// Recompute derived orientation vectors (forward, up, right)
    /// from the orientation quaternion.
    void update_orientation_vectors();

    /// Accumulated angular rate inputs (radians/sec).  Cleared each frame
    /// by the control inputs; applied in apply_rotation() during update().
    double pitch_rate_ = 0.0;
    double yaw_rate_ = 0.0;
    double roll_rate_ = 0.0;
};
