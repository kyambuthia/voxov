#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

// ── Orbital Elements ──────────────────────────────────────────────────────
// Describes a Keplerian orbit of one body around its parent.
// All angular quantities in radians, distances in meters, time in seconds.
//
// WHY: Keplerian orbits provide analytical positions without numerical
// integration, avoiding drift and enabling arbitrarily long simulation
// times.  The standard 6-element set (a, e, i, Ω, ω, M₀) plus period
// defines any elliptical orbit.
struct OrbitalElements {
    double semi_major_axis = 0.0;          // meters from parent
    double eccentricity = 0.0;             // 0 = circle, 0 < e < 1 = ellipse
    double inclination = 0.0;              // radians from reference plane
    double longitude_ascending_node = 0.0; // Ω radians
    double argument_periapsis = 0.0;       // ω radians
    double mean_anomaly_epoch = 0.0;       // M₀ radians at t = 0
    double orbital_period = 0.0;           // seconds for one full orbit
    double mass = 0.0;                     // kg (currently cosmetic)
    double radius = 0.0;                   // meters (for rendering)
};

// ── Celestial Body ────────────────────────────────────────────────────────
struct CelestialBody {
    std::string name;
    OrbitalElements orbital;
    glm::dvec3 position{0.0};        // current world position (updated each update)
    glm::dvec3 velocity{0.0};        // current world velocity
    glm::vec3 color{1.0f};           // rendering colour (RGB)
    bool is_star = false;            // true for the sun (emits light)
    int32_t parent_index = -1;       // index of parent body in SolarSystem::bodies_
};

// ── Solar System ──────────────────────────────────────────────────────────
// Manages a hierarchical set of celestial bodies that orbit each other
// according to Kepler's laws.  Positions are updated analytically each
// frame — no numerical integration drift.
//
// WHY hierarchy: each body computes its position relative to its parent,
// then adds the parent's world position.  This handles arbitrary nesting
// (star → planet → moon) without a separate co-ordinate frame stack.
class SolarSystem {
public:
    // Initialise with default bodies: Sun, Planet (voxel world), Moon.
    void init();

    // Update all body positions for the given elapsed simulation time.
    // time_seconds: total simulation time (monotonically increasing).
    void update(double time_seconds);

    // Get the current world-space position of a body by index.
    glm::dvec3 body_position(int32_t index) const;

    // Read-only access to all bodies.
    const std::vector<CelestialBody>& bodies() const { return bodies_; }

    // Position of the first body marked as a star (the sun).
    glm::dvec3 sun_position() const;

    // Unit vector from a given world point toward the sun.
    glm::dvec3 sun_direction_from(const glm::dvec3& position) const;

    int32_t body_count() const { return static_cast<int32_t>(bodies_.size()); }

private:
    std::vector<CelestialBody> bodies_;

    // Solve Kepler's equation  M = E − e·sin(E)  for E using
    // Newton–Raphson iteration.  Converges to machine precision in
    // ≤ 6 iterations for typical eccentricities.
    static double solve_kepler(double mean_anomaly, double eccentricity);

    // Convert orbital elements + true anomaly to a 3-D Cartesian
    // position in the parent body's reference frame.
    static glm::dvec3 orbital_to_cartesian(
        const OrbitalElements& oe, double true_anomaly);

    // Compute true anomaly ν from eccentric anomaly E and eccentricity e.
    static double true_anomaly_from_eccentric(
        double eccentric_anomaly, double eccentricity);
};
