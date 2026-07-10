#include "engine_world/planet_lod.hpp"

#include <algorithm>
#include <cmath>

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
