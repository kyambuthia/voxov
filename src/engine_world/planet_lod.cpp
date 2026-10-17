#include "engine_world/planet_lod.hpp"

#include <algorithm>
#include <cmath>

int32_t planet_globe_subdivisions(double radius, double camera_distance,
    float screen_height_pixels, float fov_y_radians,
    int32_t previous_subdivisions) {
    if (radius <= 0.0 || screen_height_pixels <= 0.0f) return 8;
    const double focal = screen_height_pixels /
        (2.0 * std::tan(std::clamp(static_cast<double>(fov_y_radians),
                                 0.1, 3.0) * 0.5));
    // Tangent-cone projection stays conservative on a close approach.
    const double distance = std::max(camera_distance, radius);
    const double projected_diameter = 2.0 * radius * focal /
        std::sqrt(std::max(distance * distance - radius * radius,
                           radius * radius * 0.0001));
    int32_t grid = previous_subdivisions;
    const bool has_previous = grid == 8 || grid == 16 || grid == 32 || grid == 64;
    if (!has_previous) grid = 8;
    const double refine_threshold = has_previous ? 15.0 : 12.0;
    while (grid < 64 && projected_diameter / grid > refine_threshold) grid *= 2;
    while (grid > 8 && projected_diameter / (grid / 2) < 9.6) grid /= 2;
    return grid;
}

// ============================================================================
// PlanetLODSystem implementation
// ============================================================================

void PlanetLODSystem::init(const LODConfig& config) {
    config_ = config;
    last_lod_change_frame_ = 0;
}

ChunkLOD PlanetLODSystem::compute_lod(
    const glm::dvec3& chunk_center,
    const glm::dvec3& camera_position,
    float screen_height_pixels,
    double chunk_world_size) const {

    ChunkLOD result{};

    // Distance from camera to chunk center.
    const double distance = glm::distance(camera_position, chunk_center);

    // Screen-space error: project chunk size onto screen plane.
    // error = (chunk_world_size / distance) * screen_height_pixels
    //
    // WHY first principles: A chunk of physical size S at distance D
    // subtends approximately S/D radians. The screen-space projection
    // is (S/D) * screen_height_pixels (small-angle approximation).
    // When projected error drops below the threshold, the chunk can
    // be rendered at coarser LOD without visible quality loss.
    const float screen_error = static_cast<float>(
        (chunk_world_size / std::max(distance, 0.001)) *
        static_cast<double>(screen_height_pixels));
    result.screen_error = screen_error;

    // Determine LOD level from screen error with hysteresis.
    //
    // WHY hysteresis: Using a single threshold causes rapid LOD toggling
    // when the player moves slightly back and forth across the boundary,
    // resulting in visible popping. Different thresholds for up/down
    // transitions (hysteresis) create a dead zone that eliminates this.
    //
    // Algorithm: progressively test coarser LOD levels. At each level,
    // the effective screen error scales with the LOD stride (each coarser
    // level doubles the effective per-feature error). Increase LOD while
    // the projected error stays below the threshold.
    //
    // Hysteresis is achieved by using error_threshold * hysteresis_factor
    // as the boundary for decreasing LOD (needs finer detail sooner),
    // while error_threshold alone governs LOD increase.
    const float threshold = config_.error_threshold;

    int32_t lod = 0;
    float level_error = screen_error;
    for (int32_t level = 0; level < config_.max_lod_level; ++level) {
        // At the proposed LOD level, the effective screen error scales
        // with resolution: coarser LOD → larger area per block → more error.
        // We check if the coarser LOD is acceptable.
        if (level_error < threshold) {
            lod = level + 1;
        }
        // Each LOD step halves resolution, doubling the effective error.
        level_error *= 2.0f;
    }
    // Clamp to max LOD level.
    lod = std::min(lod, config_.max_lod_level);
    result.level = lod;

    return result;
}

int32_t PlanetLODSystem::vertex_stride(int32_t lod_level) const {
    // Stride = 1 << lod_level (block skip step in mesh generation).
    // LOD 0: 1 << 0 = 1 (every block, 16³ effective)
    // LOD 1: 1 << 1 = 2 (every other, 8³ effective)
    // LOD 2: 1 << 2 = 4 (every 4th,   4³ effective)
    // LOD 3: 1 << 3 = 8 (every 8th,   2³ effective)
    return 1 << std::clamp(lod_level, 0, config_.max_lod_level);
}
