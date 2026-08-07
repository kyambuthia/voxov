#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

struct RenderVertex {
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::vec3 color{1.0f, 1.0f, 1.0f};
    glm::vec3 normal{0.0f, 0.0f, 0.0f};
    // xy repeats within a voxel face; z selects the texture-array layer.
    // A negative layer keeps debug/UI geometry vertex-coloured.
    glm::vec3 texcoord{-1.0f, -1.0f, -1.0f};
};

inline constexpr uint8_t kVegetationRenderMaterial = 250;

struct PackedVertex {
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    uint32_t color_rgba8 = 0xffffffffu;
    int16_t normal_xyz[3] = {0, 0, 0};
    uint16_t material = 0;
};

static_assert(sizeof(PackedVertex) == 24);

struct RenderMesh {
    std::vector<RenderVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<uint16_t> indices16;
    bool use_16_bit_indices = false;
    // Non-zero = stable GPU cache key; zero = transient (always re-uploaded).
    uint64_t mesh_id = 0;
    // Geometry/content identity for stable GPU-cache invalidation. A mesh can
    // change positions while keeping identical vertex and index counts (for
    // example after a camera-relative origin shift), so size alone is unsafe.
    uint64_t content_hash = 0;
    // Double-precision world-space origin for this mesh's float vertices.
    // Keeping the origin with the retained GPU mesh lets the camera rebase
    // without rebuilding every chunk and preserves precision across planets.
    glm::dvec3 world_origin{0.0};
    // Primary material (most-frequent) for draw-call batching.
    // 0 = unset/unknown; otherwise matches VoxelMaterial enum values.
    uint8_t material = 0;
    // Selects the no-cull pipeline for fallback geometry whose back face may be
    // visible from an interior/oblique camera angle. Terrain chunks stay culled.
    bool double_sided = false;
};

struct CameraRelativeOrigin {
    glm::dvec3 world_origin{0.0};
};

inline glm::vec3 camera_relative_position(
    const glm::dvec3 &world_position,
    const CameraRelativeOrigin &origin) {
    return glm::vec3(world_position - origin.world_origin);
}

struct RenderScene {
    CameraRelativeOrigin camera_origin{};
    std::vector<RenderMesh> opaque_meshes;
    std::vector<RenderMesh> wireframe_meshes;
    RenderMesh debug_world;
    RenderMesh debug_screen;
};

struct ProfilingSnapshot {
    double gameplay_cpu_ms = 0.0;
    double animation_cpu_ms = 0.0;
    double physics_cpu_ms = 0.0;
    double fixed_event_drain_cpu_ms = 0.0;
    double frame_event_drain_cpu_ms = 0.0;
    uint64_t memory_current_allocations = 0;
    uint64_t memory_total_allocations = 0;
    uint64_t memory_current_bytes = 0;
    uint64_t memory_total_bytes = 0;
};

struct RenderStats {
    double fps = 0.0;
    double cpu_ms = 0.0;
    double frame_ms = 0.0;
    double fixed_cpu_ms = 0.0;
    double render_cpu_ms = 0.0;
    ProfilingSnapshot profiling{};
    // Per-stage timing for frame profiler (smoothed).
    // WHY: identify bottlenecks — chunk gen hits noise, mesh build hits
    // face-culling+greedy meshing, GPU upload measures buffer bandwidth.
    double chunk_gen_ms = 0.0;
    double mesh_build_ms = 0.0;
    double gpu_upload_ms = 0.0;
    uint32_t draw_call_count = 0;
    uint32_t total_vertices = 0;
    uint32_t total_indices = 0;
    bool net_connected = false;
    uint32_t net_local_player_id = 0;
    uint32_t net_remote_count = 0;
    uint32_t net_tx_packets_per_sec = 0;
    uint32_t net_rx_packets_per_sec = 0;
    uint32_t net_tx_bytes_per_sec = 0;
    uint32_t net_rx_bytes_per_sec = 0;
    uint32_t fixed_steps = 0;
    uint32_t chunk_packets = 0;
    uint32_t chunk_changes = 0;
    uint32_t streamed_chunk_count = 0;
    // LOD distribution: how many chunks at each level (0..3).
    uint32_t lod_chunk_count[4] = {0, 0, 0, 0};
    bool menu_open = false;
    int menu_selected = 0;
    std::string menu_title;
    std::vector<std::string> menu_items;
    std::vector<std::string> menu_guide;
    std::string menu_status;
    std::string menu_text;
};
