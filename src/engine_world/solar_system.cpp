#include "engine_world/solar_system.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace {

constexpr double k_two_pi = 2.0 * std::numbers::pi;

// ── Initialisation helpers ──────────────────────────────────────────────

CelestialBody make_sun() {
    CelestialBody sun{};
    sun.name = "Sun";
    sun.is_star = true;
    sun.parent_index = -1;
    sun.orbital.radius = 10'000'000.0;   // stylized macro-scale star
    sun.color = glm::vec3(1.0f, 0.95f, 0.2f);  // warm yellow
    return sun;
}

CelestialBody make_planet(double playable_planet_radius) {
    CelestialBody planet{};
    planet.name = "Voxov";
    planet.parent_index = 0;   // orbits the sun
    planet.orbital.semi_major_axis = 150'000'000.0; // 150,000 km from sun
    planet.orbital.eccentricity = 0.0167;       // similar to Earth
    planet.orbital.inclination = 0.0;
    planet.orbital.longitude_ascending_node = 0.0;
    planet.orbital.argument_periapsis = 1.8;    // rad (~103°)
    planet.orbital.mean_anomaly_epoch = 0.0;    // start at periapsis
    planet.orbital.orbital_period = 3'600.0;    // one gameplay hour
    planet.orbital.radius = std::max(1.0, playable_planet_radius);
    planet.orbital.mass = 1.0e24;
    planet.color = glm::vec3(0.2f, 0.5f, 0.8f); // blue-green
    return planet;
}

CelestialBody make_moon(double playable_planet_radius) {
    CelestialBody moon{};
    moon.name = "Luna";
    moon.parent_index = 1;   // orbits the planet
    const double planet_radius = std::max(1.0, playable_planet_radius);
    moon.orbital.semi_major_axis = planet_radius * 12.0;
    moon.orbital.eccentricity = 0.05;
    moon.orbital.inclination = 0.09;          // ~5° tilt
    moon.orbital.longitude_ascending_node = 0.5;
    moon.orbital.argument_periapsis = 2.3;
    moon.orbital.mean_anomaly_epoch = 1.2;
    moon.orbital.orbital_period = 600.0;
    moon.orbital.radius = planet_radius * 0.27;
    moon.orbital.mass = 7.3e22;
    moon.color = glm::vec3(0.7f, 0.7f, 0.7f); // gray
    return moon;
}

} // namespace

// ── SolarSystem public interface ───────────────────────────────────────────

void SolarSystem::init(double playable_planet_radius) {
    bodies_.clear();
    bodies_.push_back(make_sun());     // index 0
    bodies_.push_back(make_planet(playable_planet_radius));  // index 1
    bodies_.push_back(make_moon(playable_planet_radius));    // index 2
}

void SolarSystem::update(double time_seconds) {
    // WHY parent-before-children ordering: children add their local orbital
    // position to the parent's world position.  The initialisation order
    // (sun → planet → moon) guarantees the parent is already updated.
    for (size_t i = 0; i < bodies_.size(); ++i) {
        CelestialBody& body = bodies_[i];
        const OrbitalElements& oe = body.orbital;

        if (body.is_star || body.parent_index < 0) {
            // Star or root body — fixed at origin.
            body.position = glm::dvec3(0.0);
            body.velocity = glm::dvec3(0.0);
            continue;
        }

        // Mean anomaly at current time.
        // WHY mod 2π: keeps the value numerically bounded for very long
        // simulations; sin/cos are periodic so wrapping is harmless.
        double M = oe.mean_anomaly_epoch + k_two_pi * time_seconds / oe.orbital_period;
        M = std::fmod(M, k_two_pi);

        // Solve Kepler's equation for eccentric anomaly E.
        const double E = solve_kepler(M, oe.eccentricity);

        // True anomaly ν.
        const double nu = true_anomaly_from_eccentric(E, oe.eccentricity);

        // Position in the parent body's reference frame.
        const glm::dvec3 local_pos = orbital_to_cartesian(oe, nu);

        // World position = parent world position + local offset.
        const glm::dvec3 parent_pos = bodies_[body.parent_index].position;
        body.position = parent_pos + local_pos;

        // Velocity = position delta / dt  (simple finite-difference).
        // We don't need high-fidelity velocity here; orbital speed is only
        // used for information / future physics.
        const double orbital_speed =
            k_two_pi * oe.semi_major_axis / oe.orbital_period;
        body.velocity = glm::dvec3(0.0, orbital_speed, 0.0); // placeholder
    }
}

glm::dvec3 SolarSystem::body_position(int32_t index) const {
    if (index < 0 || static_cast<size_t>(index) >= bodies_.size()) {
        return glm::dvec3(0.0);
    }
    return bodies_[index].position;
}

glm::dvec3 SolarSystem::sun_position() const {
    for (const CelestialBody& body : bodies_) {
        if (body.is_star) {
            return body.position;
        }
    }
    return glm::dvec3(0.0, 1000.0, 0.0); // fallback: directly above
}

glm::dvec3 SolarSystem::sun_direction_from(const glm::dvec3& position) const {
    const glm::dvec3 sun_pos = sun_position();
    const glm::dvec3 delta = sun_pos - position;
    const double len = glm::length(delta);
    if (len < 1e-6) {
        return glm::dvec3(0.0, 1.0, 0.0); // at sun centre — default up
    }
    return delta / len;
}

// ── Orbital mechanics (private) ────────────────────────────────────────────

double SolarSystem::solve_kepler(double M, double e) {
    // Newton–Raphson:  E_{n+1} = E_n − (E_n − e·sin(E_n) − M) / (1 − e·cos(E_n))
    //
    // WHY Newton–Raphson over bisection: converges quadratically; for
    // eccentricities < 0.9, 6 iterations give double-precision accuracy.
    // Bisection is safer for e → 1 but our orbits are near-circular.
    //
    // Initial guess: M + sign(sin(M)) · e · 0.85
    // This is a first-order correction that gets us close enough to
    // converge in ≤ 5 iterations for e < 0.95.

    // Wrap M to [0, 2π) for consistent initial guess.
    M = std::fmod(M, k_two_pi);
    if (M < 0.0) M += k_two_pi;

    const double sin_M = std::sin(M);
    double E = M + (sin_M >= 0.0 ? 1.0 : -1.0) * e * 0.85;

    // Cap the number of iterations as a safety net.
    constexpr int k_max_iter = 20;
    constexpr double k_tol = 1e-12;

    for (int iter = 0; iter < k_max_iter; ++iter) {
        const double sin_E = std::sin(E);
        const double cos_E = std::cos(E);
        const double f = E - e * sin_E - M;
        const double df = 1.0 - e * cos_E;

        const double dE = f / df;
        E -= dE;

        if (std::abs(dE) < k_tol) {
            break;
        }
    }

    return E;
}

double SolarSystem::true_anomaly_from_eccentric(double E, double e) {
    // tan(ν/2) = √((1+e)/(1−e)) · tan(E/2)
    // WHY this form avoids quadrant ambiguity: uses atan2 on
    // sqrt(1+e)*sin(E/2) and sqrt(1-e)*cos(E/2) directly.
    const double sin_half_E = std::sin(E * 0.5);
    const double cos_half_E = std::cos(E * 0.5);
    const double sqrt_1pe = std::sqrt(1.0 + e);
    const double sqrt_1me = std::sqrt(1.0 - e);
    return 2.0 * std::atan2(sqrt_1pe * sin_half_E,
                             sqrt_1me * cos_half_E);
}

glm::dvec3 SolarSystem::orbital_to_cartesian(const OrbitalElements& oe,
                                              double nu) {
    // Position in the orbital plane (polar co-ordinates).
    // r = a·(1 − e²) / (1 + e·cos(ν))
    // x_orb = r·cos(ν),  y_orb = r·sin(ν),  z_orb = 0
    //
    // WHY this formula: works for any eccentricity 0 ≤ e < 1 and gives
    // the radius directly without needing E.

    const double cos_nu = std::cos(nu);
    const double sin_nu = std::sin(nu);

    const double denom = 1.0 + oe.eccentricity * cos_nu;
    // Guard against division by zero (only happens for e ≥ 1, hyperbolic).
    const double r = (denom > 1e-12)
                         ? oe.semi_major_axis * (1.0 - oe.eccentricity * oe.eccentricity) / denom
                         : oe.semi_major_axis;

    const double x_orb = r * cos_nu;
    const double y_orb = r * sin_nu;
    const double z_orb = 0.0;

    // Rotation sequence (3-1-3 Euler angles):
    //   1. Rotate by argument of periapsis ω around z-axis
    //   2. Rotate by inclination i around x-axis
    //   3. Rotate by longitude of ascending node Ω around z-axis
    //
    // WHY 3-1-3: standard orbital-mechanics convention that orients the
    // ellipse correctly in 3-D space.

    const double c_w = std::cos(oe.argument_periapsis);
    const double s_w = std::sin(oe.argument_periapsis);
    const double c_i = std::cos(oe.inclination);
    const double s_i = std::sin(oe.inclination);
    const double c_O = std::cos(oe.longitude_ascending_node);
    const double s_O = std::sin(oe.longitude_ascending_node);

    // Step 1: rotate by ω around z.
    const double x1 = x_orb * c_w - y_orb * s_w;
    const double y1 = x_orb * s_w + y_orb * c_w;
    const double z1 = z_orb;

    // Step 2: rotate by inclination i around x.
    const double x2 = x1;
    const double y2 = y1 * c_i - z1 * s_i;
    const double z2 = y1 * s_i + z1 * c_i;

    // Step 3: rotate by Ω around z.
    const double x3 = x2 * c_O - y2 * s_O;
    const double y3 = x2 * s_O + y2 * c_O;
    const double z3 = z2;

    return glm::dvec3(x3, y3, z3);
}
