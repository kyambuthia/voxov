#pragma once

#include <glm/glm.hpp>

// ─── Atmosphere Parameters ───────────────────────────────────────────
// Mathematical model based on Nishita 1993 and the Scratchapixel
// atmospheric scattering tutorial.  Rayleigh scattering (air molecules)
// causes blue sky / red sunsets; Mie scattering (aerosols) causes
// the white haze around the sun.
//
// Key equations:
//   Density:  ρ(h) = exp(-h/H)
//   Optical depth: τ = ∫ β·ρ(s) ds  (numerical integration)
//   Transmittance: T = exp(-τ)
//   Rayleigh phase: P_R(μ) = 3/(16π)·(1+μ²)
//   Mie phase (Henyey-Greenstein):
//     P_M(μ) = 3/(8π)·(1-g²)(1+μ²) / ((2+g²)·(1+g²-2gμ)^1.5)

struct AtmosphereParams {
    double planet_radius = 500.0;          // meters (planet surface radius)
    double atmosphere_height = 50.0;       // meters above surface
    glm::dvec3 rayleigh_scattering{5.8e-6, 13.5e-6, 33.1e-6};  // β_R per meter at sea level
    double mie_scattering = 21.0e-5;       // β_M per meter at sea level (210e-6 = 21e-5)
    double rayleigh_scale_height = 8000.0; // H_R in meters
    double mie_scale_height = 1200.0;      // H_M in meters
    double mie_asymmetry = 0.76;           // g parameter (forward scattering)
    glm::dvec3 sun_direction{0.0, 1.0, 0.0};
    double sun_intensity = 20.0;
    int view_ray_samples = 16;             // numerical integration samples for view ray
    int light_ray_samples = 8;             // numerical integration samples for sun shadow ray
};

struct AtmosphereState {
    glm::vec3 sky_color{0.3f, 0.5f, 0.8f};  // approximate HDR sky color at camera view
    glm::vec3 sun_color{1.0f, 0.9f, 0.6f};
    float sun_angle_cos = 0.7f;              // cos(angle between view center and sun)
    bool enabled = true;
};

struct AtmosphereUniforms {
    // Packed for std140 layout in GPU uniform block.
    // WHY std140: sokol requires explicit GLSL uniform block layout.
    // Each vec4 is 16 bytes, tightly packed.
    glm::vec4 planet_center_radius;      // xyz=center, w=radius
    glm::vec4 atmosphere_params_1;       // x=atmosphere_height, y=rayleigh_scale_height,
                                         // z=mie_scale_height, w=mie_asymmetry
    glm::vec4 rayleigh_scattering_unused; // xyz=β_R, w=unused
    glm::vec4 mie_and_padding;            // x=mie_scattering, yzw=unused
    glm::vec4 sun_direction_intensity;    // xyz=sun_dir, w=intensity
    // Total: 5 vec4 = 80 bytes
};
static_assert(sizeof(AtmosphereUniforms) == 80, "AtmosphereUniforms must be 80 bytes for std140");

class AtmosphereRenderer {
public:
    void init(const AtmosphereParams& params);

    // Compute atmosphere color for a view ray.
    // origin: camera position (planet-centered world space)
    // direction: view direction (normalized)
    // max_dist: maximum ray distance (-1 = atmosphere exit)
    // Returns: HDR sky color (may exceed 1.0; needs tone mapping)
    AtmosphereState compute_sky_color(
        const glm::dvec3& origin,
        const glm::dvec3& direction,
        double max_dist = -1.0) const;

    // Compute transmittance along a view ray from origin to point.
    // Used for aerial perspective: terrain = base_color * transmittance + inscatter.
    glm::dvec3 compute_transmittance(
        const glm::dvec3& origin,
        const glm::dvec3& target) const;

    // Get packed uniforms for GPU shader upload.
    AtmosphereUniforms gpu_uniforms() const;

    // Get atmosphere params for shader uniform.
    const AtmosphereParams& params() const { return params_; }
    const AtmosphereState& state() const { return state_; }

    // Update sun direction (e.g. from solar system orbital mechanics).
    void set_sun_direction(const glm::dvec3& dir) { params_.sun_direction = dir; }

private:
    AtmosphereParams params_;
    mutable AtmosphereState state_{};

    // Ray-sphere intersection for atmosphere outer shell.
    // Returns true if ray hits atmosphere, sets t_min and t_max.
    bool ray_atmosphere_intersect(
        const glm::dvec3& origin,
        const glm::dvec3& direction,
        double& t_min,
        double& t_max) const;

    // Compute optical depth along a ray segment using numerical integration.
    // Returns (rayleigh_depth, mie_depth).
    std::pair<glm::dvec3, double> compute_optical_depth(
        const glm::dvec3& start,
        const glm::dvec3& end,
        int samples) const;

    // Rayleigh phase function: P_R(μ)
    static double rayleigh_phase(double mu);

    // Mie phase function (Henyey-Greenstein): P_M(μ, g)
    static double mie_phase(double mu, double g);
};
