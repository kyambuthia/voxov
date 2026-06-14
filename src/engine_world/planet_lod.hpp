#pragma once
// ============================================================================
// planet_lod — distance-based chunk LOD selection
//
// Computes screen-space error for chunk centers relative to the camera,
// applies hysteresis to prevent popping, and returns the appropriate
// LOD level (0 = full res, 1 = half, 2 = quarter, etc.).
//
// LOD affects mesh vertex density: at LOD N, only every (2^N)th block
// is meshed, reducing GPU vertex count for distant chunks.
// ============================================================================

#include <cstdint>

#include <glm/glm.hpp>

// ── LOD result ──────────────────────────────────────────────────────────────

struct ChunkLOD {
    int32_t level        = 0;      // 0 = full res, 1 = half, 2 = quarter
    float   screen_error = 0.0f;   // projected screen-space error in pixels
};

// ── LOD configuration ───────────────────────────────────────────────────────

struct LODConfig {
    float   error_threshold  = 4.0f;   // pixels — switch LOD when error < this
    float   hysteresis_factor = 1.5f;  // prevent popping: up/down thresholds differ
    int32_t max_lod_level    = 3;      // 0, 1, 2, 3
};

// ── LOD system ──────────────────────────────────────────────────────────────

class PlanetLODSystem {
public:
    void init(const LODConfig& config);

    // Compute LOD for a chunk given its world-space center and camera position.
    // screen_height_pixels: current viewport height in pixels.
    // chunk_world_size: physical length of one chunk edge in meters.
    ChunkLOD compute_lod(
        const glm::dvec3& chunk_center,
        const glm::dvec3& camera_position,
        float screen_height_pixels,
        double chunk_world_size) const;

    // Get vertex stride for a LOD level (1 << level).
    // This is the block skip step in mesh generation:
    // At LOD 0: stride 1 (every block) → 16³ effective
    // At LOD 1: stride 2 (every other) → 8³ effective
    // At LOD 2: stride 4             → 4³ effective
    // At LOD 3: stride 8             → 2³ effective
    int32_t vertex_stride(int32_t lod_level) const;

    // Accessors for LOD stats.
    const LODConfig& config() const { return config_; }
    uint64_t last_lod_change_frame() const { return last_lod_change_frame_; }

private:
    LODConfig config_;
    uint64_t  last_lod_change_frame_ = 0;
};
