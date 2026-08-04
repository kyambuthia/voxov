#pragma once
// ── Coordinate Frame System ─────────────────────────────────────────────
// Hierarchical coordinate frames for precision across planet-to-solar scales.
//
// WHY: float32 fails at planet scale (500m+), float64 fails at solar scale
// (AU).  Each frame is offset from its parent, and camera-relative rendering
// uses the Local frame for GPU float32 precision.
//
// Transform chain: Local → Planet → Orbital → Solar
//   Local:    Player-relative (float32 for GPU vertex submission)
//   Planet:   Planet-centered (float64, surface walking)
//   Orbital:  Parent body relative (float64, space flight)
//   Solar:    Heliocentric (float64, interplanetary travel)
//
// Frame transitions:
//   - altitude < atmosphere_height → Planet frame (on surface)
//   - altitude >= atmosphere_height → Orbital frame (in space)
//   - entering another body's SOI → switch to that body's Orbital frame

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <vector>

class SolarSystem;

enum class CoordinateFrame {
    Local,    // Player-relative (float32 for GPU)
    Planet,   // Planet-centered (float64)
    Orbital,  // Parent body relative (float64)
    Solar,    // Heliocentric (float64)
};

/// Transform between two coordinate frames.
/// Represents an affine transformation: position' = rotation * position + origin
struct FrameTransform {
    glm::dvec3 origin{0.0};                    // Frame origin in parent frame
    glm::dquat rotation{1.0, 0.0, 0.0, 0.0};  // Frame orientation
    double scale = 1.0;                        // For LOD transitions
};

/// Manages coordinate frame hierarchy and transitions for inter-planetary travel.
///
/// The transform chain is:
///   Solar (heliocentric) → Orbital (body-centered) → Planet (surface)
///
/// When the player is on the surface, we work in Planet frame.  When the
/// vehicle exceeds atmosphere height, we switch to Orbital frame.  When
/// the vehicle enters another body's SOI, we switch parent.
class CoordinateFrameManager {
public:
    /// Initialize with default transforms (all identity).
    void init();

    /// Convert a position between coordinate frames.
    ///
    /// @param position  The position to convert.
    /// @param from      Source coordinate frame.
    /// @param to        Destination coordinate frame.
    /// @param body_index  Index of the body whose frame to use (0=sun, 1=Voxov, 2=Luna, 3=Aster).
    /// @return The position in the destination frame.
    ///
    /// WHY body_index: different bodies have different positions in the
    /// parent frame; the planet and moon are at different orbital positions.
    glm::dvec3 transform(
        const glm::dvec3& position,
        CoordinateFrame from,
        CoordinateFrame to,
        int32_t body_index = 0) const;

    /// Determine the current coordinate frame based on player altitude.
    ///
    /// @param altitude           Height above planet surface in meters.
    /// @param atmosphere_height  Top of atmosphere in meters above surface.
    /// @return CoordinateFrame::Planet if below atmosphere, else Orbital.
    CoordinateFrame current_frame(double altitude, double atmosphere_height) const;

    /// Get the transform for a specific body (planet position in parent frame).
    const FrameTransform& body_transform(int32_t index) const;

    /// Update transforms from the solar system simulation.
    ///
    /// Call once per frame after SolarSystem::update() to keep the coordinate
    /// frame transforms in sync with orbital positions.
    void update(const SolarSystem& solar_system);

    // ── SOI (Sphere of Influence) helpers ───────────────────────────────

    /// Compute the Sphere of Influence radius for a body.
    ///
    /// SOI radius = semi_major_axis × (mass_body / mass_parent)^(2/5)
    ///
    /// WHY this formula: from patched conic approximation; when a vehicle
    /// enters this radius, the body's gravity dominates the parent's.  This
    /// determines when to switch coordinate frames.
    ///
    /// @param semi_major_axis  Orbital distance from parent (meters).
    /// @param mass_body        Mass of the body (kg).
    /// @param mass_parent      Mass of the parent body (kg).
    /// @return SOI radius in meters.
    static double compute_soi_radius(
        double semi_major_axis,
        double mass_body,
        double mass_parent);

    /// Detect which body's SOI contains a given position.
    ///
    /// @param position   Position in Solar (heliocentric) frame.
    /// @param solar_system The solar system to query for body positions/masses.
    /// @return Index of the body whose SOI contains the position, or -1 if none.
    int32_t detect_soi(
        const glm::dvec3& position,
        const SolarSystem& solar_system) const;

private:
    /// Body transforms: position + orientation of each body in its parent frame.
    /// Index 0 = Sun (parent: none), 1 = Planet (parent: Sun), 2 = Moon (parent: Planet).
    std::vector<FrameTransform> body_transforms_;

    // Absolute body origins in Solar space. FrameTransform::origin is kept
    // parent-relative for diagnostics, but transforms need the accumulated
    // origin when the active body is Luna/Aster rather than Voxov.
    std::vector<glm::dvec3> body_solar_positions_;

    /// Transform from Solar (heliocentric) frame to planet-centered frame.
    /// Used when converting between Orbital and Planet frames for the voxel world.
    FrameTransform solar_to_planet_;
};
