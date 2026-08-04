#include "engine_world/coordinate_frames.hpp"
#include "engine_world/solar_system.hpp"

#include <algorithm>
#include <cmath>

void CoordinateFrameManager::init() {
    body_transforms_.clear();
    body_solar_positions_.clear();
    // Default bodies: Sun (0), Voxov (1), Luna (2), Aster (3).
    // Sun is at origin in Solar frame (no parent).
    // Planet orbits the Sun.
    // Moon orbits the Planet.
    body_transforms_.resize(4);
    body_solar_positions_.resize(4, glm::dvec3(0.0));
    for (auto& t : body_transforms_) {
        t.origin = glm::dvec3(0.0);
        t.rotation = glm::dquat(1.0, 0.0, 0.0, 0.0);
        t.scale = 1.0;
    }
    solar_to_planet_ = FrameTransform{};
}

glm::dvec3 CoordinateFrameManager::transform(
    const glm::dvec3& position,
    CoordinateFrame from,
    CoordinateFrame to,
    int32_t body_index) const {
    if (from == to) return position;

    // Planet and Orbital are both body-centered representations. Their
    // relationship is an identity transform; only crossing the Solar
    // boundary changes the origin. This keeps the operation stable for
    // arbitrary body indices and avoids the old Voxov-only special case.
    const int32_t resolved_body =
        (body_index >= 0 &&
         static_cast<size_t>(body_index) < body_solar_positions_.size())
            ? body_index
            : 1;
    const glm::dvec3 body_origin = body_solar_positions_.empty()
        ? glm::dvec3(0.0)
        : body_solar_positions_[static_cast<size_t>(resolved_body)];

    if (from == CoordinateFrame::Solar && to != CoordinateFrame::Solar) {
        return position - body_origin;
    }
    if (from != CoordinateFrame::Solar && to == CoordinateFrame::Solar) {
        return position + body_origin;
    }
    return position;
}

CoordinateFrame CoordinateFrameManager::current_frame(
    double altitude, double atmosphere_height) const {
    // WHY: below atmosphere = surface walking (Planet frame),
    // above atmosphere = space flight (Orbital frame).
    // Hysteresis is not applied here — the caller should add hysteresis
    // to prevent rapid frame toggling at the boundary.
    if (altitude < atmosphere_height) {
        return CoordinateFrame::Planet;
    }
    return CoordinateFrame::Orbital;
}

const FrameTransform& CoordinateFrameManager::body_transform(int32_t index) const {
    static FrameTransform identity{};
    if (index < 0 || static_cast<size_t>(index) >= body_transforms_.size()) {
        return identity;
    }
    return body_transforms_[index];
}

void CoordinateFrameManager::update(const SolarSystem& solar_system) {
    // Ensure we have enough transforms for all bodies.
    const int32_t count = solar_system.body_count();
    if (static_cast<int32_t>(body_transforms_.size()) < count) {
        body_transforms_.resize(static_cast<size_t>(count));
    }
    if (static_cast<int32_t>(body_solar_positions_.size()) < count) {
        body_solar_positions_.resize(static_cast<size_t>(count),
                                     glm::dvec3(0.0));
    }

    // Update each body's transform from the solar system.
    // The transform origin is the body's position in the PARENT frame,
    // NOT the world frame.  The SolarSystem stores positions in world
    // (heliocentric) frame, so we need to subtract the parent's position.
    for (int32_t i = 0; i < count; ++i) {
        const CelestialBody& body = solar_system.bodies()[static_cast<size_t>(i)];
        FrameTransform& t = body_transforms_[static_cast<size_t>(i)];
        body_solar_positions_[static_cast<size_t>(i)] = body.position;

        if (body.is_star || body.parent_index < 0) {
            // Root body (Sun) — at origin of Solar frame.
            t.origin = glm::dvec3(0.0);
        } else {
            // Body's position in parent frame = world_pos - parent_world_pos.
            const glm::dvec3 parent_pos =
                solar_system.body_position(body.parent_index);
            t.origin = body.position - parent_pos;
        }
        t.rotation = glm::dquat(1.0, 0.0, 0.0, 0.0);
        t.scale = 1.0;
    }
}

double CoordinateFrameManager::compute_soi_radius(
    double semi_major_axis,
    double mass_body,
    double mass_parent) {
    // SOI radius = a × (m_body / m_parent)^(2/5)
    // WHY exponent 2/5: derived from the ratio of the body's gravitational
    // perturbation to the parent's; standard patched conic approximation.
    if (semi_major_axis <= 0.0 || mass_parent <= 0.0 || mass_body <= 0.0) {
        return 0.0;
    }

    const double ratio = mass_body / mass_parent;
    // pow(x, 0.4) = x^(2/5)
    return semi_major_axis * std::pow(ratio, 0.4);
}

int32_t CoordinateFrameManager::detect_soi(
    const glm::dvec3& position,
    const SolarSystem& solar_system) const {
    // Check each body (except the sun) to see if the position is within
    // its SOI.  Start from the innermost (highest index) to give priority
    // to moons over planets.
    const int32_t count = solar_system.body_count();
    int32_t best_body = -1;
    double best_distance = std::numeric_limits<double>::max();

    for (int32_t i = count - 1; i >= 0; --i) {
        const CelestialBody& body = solar_system.bodies()[static_cast<size_t>(i)];
        if (body.is_star) continue;  // Sun IS the Solar frame — no SOI check
        if (body.parent_index < 0) continue;

        const CelestialBody& parent =
            solar_system.bodies()[static_cast<size_t>(body.parent_index)];

        const double soi = compute_soi_radius(
            body.orbital.semi_major_axis,
            body.orbital.mass,
            parent.orbital.mass);

        if (soi <= 0.0) continue;

        const double dist = glm::length(position - body.position);
        if (dist <= soi && dist < best_distance) {
            best_distance = dist;
            best_body = i;
        }
    }

    return best_body;
}
