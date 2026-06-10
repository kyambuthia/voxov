#pragma once
// ============================================================================
// planet_blocks — cube-sphere voxel planet (Bowerbyte/Peck architecture)
//
// Planet built from cubic blocks on a quad sphere (6 sectors, one per cube
// face).  Blocks are gravity-aligned: top face points to space, bottom to
// planet center.  Sectors subdivided into shells with doubling horizontal
// resolution per axis each shell; shells into fixed 16³ chunks.
//
// Key components:
//   BlockAddress      — hierarchical (sector, shell, chunk, block)
//   BlockWorld        — chunk storage, generation, meshing, queries
//   CubeNet           — 12 face-edge pairings for cross-sector neighbors
//   SphereNoise3D     — seamless 3D noise sampled on sphere surface
// ============================================================================

#include "engine_render/render_types.hpp"
#include "engine_world/planet.hpp"
#include "engine_world/voxel_chunk.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

// ── Block address ──────────────────────────────────────────────────────────

struct BlockAddress {
    PlanetFace sector = PlanetFace::PosY;   // 0–5, one per cube face
    int32_t    shell  = 0;                   // 0..N, radial layer
    glm::ivec3 chunk  = glm::ivec3(0);       // chunk index within shell
    glm::ivec3 block  = glm::ivec3(0);       // block index within chunk [0..15]

    friend bool operator==(const BlockAddress &a,
                           const BlockAddress &b) = default;
};

struct BlockAddressHash {
    size_t operator()(const BlockAddress &a) const noexcept;
};

// ── Shell configuration ────────────────────────────────────────────────────

struct ShellConfig {
    int32_t index          = 0;   // 0 = innermost
    double  inner_radius   = 0.0; // distance from planet center
    double  outer_radius   = 0.0;
    int32_t horizontal_res = 0;   // blocks per axis on one face (power of 2)
    int32_t vertical_layers = 0;  // radial layers in this shell
};

// ── Cube net edge pairing ──────────────────────────────────────────────────

// Describes how two cube faces share an edge.
// When walking off face `from` at edge `from_edge`, you arrive on face `to`
// with the local axes potentially swapped or flipped.
enum class CubeEdge : uint8_t {
    Left  = 0,  // u = -1
    Right = 1,  // u = +1
    Top   = 2,  // v = +1
    Bottom= 3,  // v = -1
};

struct CubeEdgePairing {
    PlanetFace from_face;
    CubeEdge   from_edge;
    PlanetFace to_face;
    CubeEdge   to_edge;
    bool       swap_uv = false;   // u↔v when crossing
    bool       flip_u  = false;   // u axis reversed
    bool       flip_v  = false;   // v axis reversed
};

// ── Block world ────────────────────────────────────────────────────────────

// Directions for neighbor lookup (local chunk axes).
enum class BlockDir : uint8_t {
    Left  = 0, Right = 1,
    Down  = 2, Up    = 3,
    Back  = 4, Front = 5,
};

struct BlockNeighbor {
    BlockAddress address;
    bool         smaller = false;  // neighbor has fewer blocks (shell down)
    bool         larger  = false;  // neighbor has more blocks (shell up)
    int32_t      sub_x   = 0;      // if larger, which sub-block (0 or 1)
    int32_t      sub_z   = 0;
};

struct BlockWorldConfig {
    PlanetDefinition planet;
    int32_t surface_shells   = 4;     // number of shells from core to surface
    int32_t base_resolution  = 8;     // blocks per axis on innermost shell
    double  block_size       = 1.0;   // meters, target block width at surface
    int32_t chunk_size       = 16;    // blocks per chunk edge
    uint64_t seed            = 0;
};

class BlockWorld {
public:
    BlockWorld() = default;

    void init(const BlockWorldConfig &config);
    bool initialized() const { return initialized_; }

    // ── Queries ────────────────────────────────────────────────────────
    BlockAddress address_from_world(const glm::dvec3 &world_pos) const;
    glm::dvec3   world_from_address(const BlockAddress &addr) const;

    std::vector<BlockNeighbor> neighbors(const BlockAddress &addr,
                                         BlockDir dir) const;
    BlockDir world_dir_to_block_dir(const glm::dvec3 &world_pos,
                                    const glm::dvec3 &world_dir) const;

    // ── Terrain ────────────────────────────────────────────────────────
    int32_t terrain_height_at(const glm::dvec3 &world_dir) const;
    int32_t terrain_height_at_face_uv(PlanetFace face, int32_t col_x,
                                      int32_t col_z) const;
    VoxelMaterial block_material_at(const BlockAddress &addr,
                                    int32_t surface_height) const;

    // ── Generation ─────────────────────────────────────────────────────
    void generate_chunk(const BlockAddress &addr, VoxelChunk &out) const;
    RenderMesh build_chunk_mesh(const BlockAddress &addr,
                                 const VoxelChunk &chunk,
                                 const std::function<bool(const BlockAddress&)> &solid_at,
                                 const glm::dvec3 &camera_relative_origin = glm::dvec3(0.0)) const;

    // ── Streaming ──────────────────────────────────────────────────────
    VoxelChunk &get_or_generate_chunk(const BlockAddress &addr);
    const VoxelChunk *find_chunk(const BlockAddress &addr) const;
    size_t chunk_count() const { return chunks_.size(); }

    // ── Stable mesh ID ──────────────────────────────────────────────────
    // Deterministic uint64 from sector+shell+chunk for GPU cache key.
    static uint64_t chunk_mesh_id(const BlockAddress &addr);

    // ── Shell info ─────────────────────────────────────────────────────
    const ShellConfig &shell_config(int32_t shell) const;
    int32_t shell_count() const { return static_cast<int32_t>(shells_.size()); }
    const BlockWorldConfig &config() const { return config_; }
    const PlanetDefinition &planet() const { return config_.planet; }

    // ── Cube net ───────────────────────────────────────────────────────
    static const CubeEdgePairing &edge_pairing(PlanetFace from, CubeEdge edge);
    static const std::array<CubeEdgePairing, 12> &all_edge_pairings();

private:
    // ── Internal helpers ───────────────────────────────────────────────
    void build_shells();
    glm::dvec3 block_world_center(PlanetFace face, int32_t shell_idx,
                                  int32_t col_x, int32_t col_z,
                                  int32_t layer_y) const;
    void block_face_uv(PlanetFace face, int32_t shell_idx,
                       int32_t col_x, int32_t col_z,
                       double &u, double &v) const;
    void chunk_face_uv_range(const BlockAddress &addr,
                             double &u0, double &v0,
                             double &u1, double &v1) const;
    glm::dvec3 block_forward_dir(PlanetFace face, int32_t col_x,
                                 int32_t col_z) const;

    BlockWorldConfig config_{};
    std::vector<ShellConfig> shells_{};
    std::unordered_map<BlockAddress, VoxelChunk, BlockAddressHash> chunks_{};
    bool initialized_ = false;
};

// ── 3D noise on sphere surface ─────────────────────────────────────────────

// Seamless terrain height via 3D noise sampled on the unit sphere.
// Different radii and translations give different noise scales and seeds.
class SphereNoise3D {
public:
    explicit SphereNoise3D(uint64_t seed = 0);

    // Sample noise value in [-1, 1] at a unit-sphere direction.
    float sample(const glm::dvec3 &direction, float frequency = 1.0f) const;

    // Fractional Brownian motion (multi-octave) on sphere surface.
    float fbm(const glm::dvec3 &direction,
              int octaves = 4,
              float lacunarity = 2.0f,
              float gain = 0.5f) const;

    // Terrain height in voxels at a sphere direction.
    int32_t terrain_height(const glm::dvec3 &direction,
                           float base_height = 12.0f,
                           float amplitude = 10.0f) const;

private:
    float value_noise(const glm::dvec3 &p) const;
    uint64_t seed_ = 0;
};

// ── Block direction helpers ────────────────────────────────────────────────

glm::ivec3 block_dir_vector(BlockDir dir);
BlockDir block_dir_opposite(BlockDir dir);
const char *block_dir_name(BlockDir dir);
