#include "engine_world/atmosphere.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>

namespace {
    constexpr double kPi = glm::pi<double>();
    constexpr double kRayleighPhaseNorm = 3.0 / (16.0 * kPi);
    constexpr double kMiePhaseNorm = 3.0 / (8.0 * kPi);
}

void AtmosphereRenderer::init(const AtmosphereParams& params) {
    params_ = params;
    state_ = AtmosphereState{};
}

bool AtmosphereRenderer::ray_atmosphere_intersect(
    const glm::dvec3& origin,
    const glm::dvec3& direction,
    double& t_min,
    double& t_max) const
{
    // Ray-sphere intersection: solve |O + t*D|² = R²
    // a = D·D = 1 (direction is normalized)
    // b = 2*(O·D)
    // c = |O|² - R²
    const double R = params_.planet_radius + params_.atmosphere_height;
    const double R2 = R * R;
    const double b = 2.0 * glm::dot(origin, direction);
    const double c = glm::dot(origin, origin) - R2;
    const double disc = b * b - 4.0 * c;

    if (disc < 0.0) {
        return false;
    }

    const double sqrt_disc = std::sqrt(disc);
    t_min = (-b - sqrt_disc) * 0.5;
    t_max = (-b + sqrt_disc) * 0.5;

    // If both intersections are behind the ray, no valid hit.
    if (t_max <= 0.0) {
        return false;
    }

    // Clamp t_min to 0 if origin is inside atmosphere.
    if (t_min < 0.0) {
        t_min = 0.0;
    }

    return true;
}

std::pair<glm::dvec3, double> AtmosphereRenderer::compute_optical_depth(
    const glm::dvec3& start,
    const glm::dvec3& end,
    int samples) const
{
    glm::dvec3 depth_r(0.0);
    double depth_m = 0.0;

    const glm::dvec3 dir = end - start;
    const double dist = glm::length(dir);
    if (dist < 1e-9) {
        return {depth_r, depth_m};
    }

    const glm::dvec3 step_dir = dir / dist;
    const double step_size = dist / static_cast<double>(samples);
    const double R_planet = params_.planet_radius;

    for (int i = 0; i < samples; ++i) {
        // Sample at segment midpoint for better convergence.
        const double t = (static_cast<double>(i) + 0.5) * step_size;
        const glm::dvec3 p = start + step_dir * t;
        const double r = glm::length(p);
        const double h = r - R_planet;

        // Inside planet — no scattering (opaque ground).
        if (h < 0.0) {
            break;
        }

        const double density_r = std::exp(-h / params_.rayleigh_scale_height);
        const double density_m = std::exp(-h / params_.mie_scale_height);

        depth_r += params_.rayleigh_scattering * density_r * step_size;
        depth_m += params_.mie_scattering * density_m * step_size;
    }

    return {depth_r, depth_m};
}

double AtmosphereRenderer::rayleigh_phase(double mu) {
    return kRayleighPhaseNorm * (1.0 + mu * mu);
}

double AtmosphereRenderer::mie_phase(double mu, double g) {
    const double g2 = g * g;
    const double num = (1.0 - g2) * (1.0 + mu * mu);
    const double denom_1 = 2.0 + g2;
    const double denom_2 = std::pow(1.0 + g2 - 2.0 * g * mu, 1.5);
    return kMiePhaseNorm * num / (denom_1 * denom_2);
}

AtmosphereState AtmosphereRenderer::compute_sky_color(
    const glm::dvec3& origin,
    const glm::dvec3& direction,
    double max_dist) const
{
    AtmosphereState result{};
    result.enabled = true;

    const glm::dvec3 dir_norm = glm::normalize(direction);

    // Compute cos(angle) between view direction and sun direction for phase functions.
    const double mu = glm::dot(dir_norm, glm::normalize(params_.sun_direction));
    result.sun_angle_cos = static_cast<float>(mu);

    // Ray-sphere intersection: find where view ray enters/exits atmosphere.
    double t_min, t_max;
    if (!ray_atmosphere_intersect(origin, dir_norm, t_min, t_max)) {
        // Camera is outside atmosphere and ray doesn't hit it.
        result.sky_color = glm::vec3(0.0f);
        result.sun_color = glm::vec3(0.0f);
        return result;
    }

    // Clamp max distance if specified (for terrain intersection).
    if (max_dist > 0.0 && max_dist < t_max) {
        t_max = max_dist;
    }
    if (t_max <= t_min) {
        result.sky_color = glm::vec3(0.0f);
        result.sun_color = glm::vec3(0.0f);
        return result;
    }

    // Phase functions (angle between view and sun).
    const double phase_r = rayleigh_phase(mu);
    const double phase_m = mie_phase(mu, params_.mie_asymmetry);

    const int view_samples = params_.view_ray_samples;
    const int light_samples = params_.light_ray_samples;
    const double segment_length = (t_max - t_min) / static_cast<double>(view_samples);
    const double R_planet = params_.planet_radius;

    // Accumulated optical depth along the view ray (from origin to current sample).
    glm::dvec3 optical_depth_r(0.0);
    double optical_depth_m = 0.0;

    // Accumulated scattered light (Rayleigh + Mie).
    glm::dvec3 sum_r(0.0);
    glm::dvec3 sum_m(0.0);

    double t_current = t_min;
    for (int i = 0; i < view_samples; ++i) {
        const glm::dvec3 sample_pos = origin + dir_norm * (t_current + segment_length * 0.5);
        const double h = glm::length(sample_pos) - R_planet;

        if (h < 0.0) {
            // Below planet surface — stop accumulating.
            break;
        }

        // Density at sample point.
        const double density_r = std::exp(-h / params_.rayleigh_scale_height);
        const double density_m = std::exp(-h / params_.mie_scale_height);

        // Increment view-ray optical depth (used for transmittance from sample to origin).
        const double hr = density_r * segment_length;
        const double hm = density_m * segment_length;
        optical_depth_r += params_.rayleigh_scattering * hr;
        optical_depth_m += params_.mie_scattering * hm;

        // Optical depth from sample toward sun (shadow ray).
        glm::dvec3 light_depth_r(0.0);
        double light_depth_m = 0.0;

        double t0_light, t1_light;
        if (ray_atmosphere_intersect(sample_pos, params_.sun_direction, t0_light, t1_light)) {
            // Clamp light ray to atmosphere bounds.
            if (t1_light > 0.0) {
                const double light_segment = t1_light / static_cast<double>(light_samples);
                double t_light = 0.0;
                bool shadowed = false;
                for (int j = 0; j < light_samples; ++j) {
                    const glm::dvec3 light_pos =
                        sample_pos + params_.sun_direction * (t_light + light_segment * 0.5);
                    const double h_light = glm::length(light_pos) - R_planet;
                    if (h_light < 0.0) {
                        shadowed = true;
                        break; // Ray enters planet — shadowed.
                    }
                    const double ldr = std::exp(-h_light / params_.rayleigh_scale_height) * light_segment;
                    const double ldm = std::exp(-h_light / params_.mie_scale_height) * light_segment;
                    light_depth_r += params_.rayleigh_scattering * ldr;
                    light_depth_m += params_.mie_scattering * ldm;
                    t_light += light_segment;
                }

                if (!shadowed) {
                    // Combined optical depth: view ray + light ray.
                    // WHY combine: T_view * T_light = exp(-(τ_view + τ_light))
                    const glm::dvec3 total_r = optical_depth_r + light_depth_r;
                    // Mie extinction is ~1.1x the scattering coefficient.
                    const double total_m = optical_depth_m * 1.1 + light_depth_m * 1.1;

                    const glm::dvec3 attenuation(
                        std::exp(-total_r.x),
                        std::exp(-total_r.y),
                        std::exp(-total_r.z));
                    const double attenuation_m = std::exp(-total_m);

                    sum_r += attenuation * hr;
                    sum_m += glm::dvec3(attenuation_m * hm);
                }
            }
        }

        t_current += segment_length;
    }

    // Scale by scattering coefficients and phase functions.
    const glm::dvec3 rayleigh_contrib = sum_r * params_.rayleigh_scattering * phase_r;
    const glm::dvec3 mie_contrib = sum_m * params_.mie_scattering * phase_m;
    const glm::dvec3 total = (rayleigh_contrib + mie_contrib) * params_.sun_intensity;

    result.sky_color = glm::vec3(total);
    result.sun_color = glm::vec3(glm::clamp(total, 0.0, 5.0));

    return result;
}

glm::dvec3 AtmosphereRenderer::compute_transmittance(
    const glm::dvec3& origin,
    const glm::dvec3& target) const
{
    const glm::dvec3 dir = target - origin;
    const double dist = glm::length(dir);
    if (dist < 1e-9) {
        return glm::dvec3(1.0);
    }

    const glm::dvec3 step_dir = dir / dist;

    // Check if ray goes through atmosphere at all.
    double t0, t1;
    if (!ray_atmosphere_intersect(origin, step_dir, t0, t1)) {
        return glm::dvec3(1.0); // No atmosphere along path.
    }

    // Clamp to [0, dist].
    const double t_start = std::max(0.0, t0);
    const double t_end = std::min(dist, t1);

    if (t_end <= t_start) {
        return glm::dvec3(1.0);
    }

    // Compute optical depth from origin to target along the path segment
    // that lies within the atmosphere.
    const glm::dvec3 atm_start = origin + step_dir * t_start;
    const glm::dvec3 atm_end   = origin + step_dir * t_end;

    auto [depth_r, depth_m] = compute_optical_depth(atm_start, atm_end, params_.view_ray_samples);
    // Mie extinction ≈ 1.1 × scattering.
    const double depth_m_ext = depth_m * 1.1;

    return glm::dvec3(
        std::exp(-depth_r.x - depth_m_ext),
        std::exp(-depth_r.y - depth_m_ext),
        std::exp(-depth_r.z - depth_m_ext));
}

AtmosphereUniforms AtmosphereRenderer::gpu_uniforms() const {
    AtmosphereUniforms u{};
    u.planet_center_radius = glm::vec4(
        glm::vec3(0.0),
        static_cast<float>(params_.planet_radius));
    u.atmosphere_params_1 = glm::vec4(
        static_cast<float>(params_.atmosphere_height),
        static_cast<float>(params_.rayleigh_scale_height),
        static_cast<float>(params_.mie_scale_height),
        static_cast<float>(params_.mie_asymmetry));
    u.rayleigh_scattering_unused = glm::vec4(
        glm::vec3(params_.rayleigh_scattering),
        0.0f);
    u.mie_and_padding = glm::vec4(
        static_cast<float>(params_.mie_scattering),
        0.0f, 0.0f, 0.0f);
    u.sun_direction_intensity = glm::vec4(
        glm::vec3(params_.sun_direction),
        static_cast<float>(params_.sun_intensity));
    return u;
}
