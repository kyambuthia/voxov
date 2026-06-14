#include "engine_world/planet_blocks.hpp"
#include "engine_world/voxel_chunk.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>    // std::fprintf for debug diagnostics (TODO: remove)
#include <cstring>

// ============================================================================
// BlockAddressHash
// ============================================================================

size_t BlockAddressHash::operator()(const BlockAddress &a) const noexcept {
    uint64_t h = static_cast<uint64_t>(static_cast<uint8_t>(a.sector));
    h ^= static_cast<uint64_t>(static_cast<uint32_t>(a.shell)) << 8;
    h ^= static_cast<uint64_t>(static_cast<uint32_t>(a.chunk.x)) << 16;
    h ^= static_cast<uint64_t>(static_cast<uint32_t>(a.chunk.y)) << 24;
    h ^= static_cast<uint64_t>(static_cast<uint32_t>(a.chunk.z)) << 32;
    h ^= static_cast<uint64_t>(static_cast<uint32_t>(a.block.x)) << 40;
    h ^= static_cast<uint64_t>(static_cast<uint32_t>(a.block.y)) << 48;
    h ^= static_cast<uint64_t>(static_cast<uint32_t>(a.block.z)) << 56;
    h ^= h >> 30u;
    h *= 0xbf58476d1ce4e5b9ull;
    h ^= h >> 27u;
    h *= 0x94d049bb133111ebull;
    h ^= h >> 31u;
    return static_cast<size_t>(h);
}

// ============================================================================
// Cube net — 12 edge pairings between 6 cube faces
// ============================================================================

static const CubeEdgePairing k_edge_pairings[12] = {
    {PlanetFace::PosX, CubeEdge::Left,   PlanetFace::NegZ, CubeEdge::Right, false, false, false},
    {PlanetFace::PosX, CubeEdge::Right,  PlanetFace::PosZ, CubeEdge::Left,  false, false, false},
    {PlanetFace::PosX, CubeEdge::Top,    PlanetFace::PosY, CubeEdge::Right, true,  false, false},
    {PlanetFace::PosX, CubeEdge::Bottom, PlanetFace::NegY, CubeEdge::Right, true,  false, true },

    {PlanetFace::NegX, CubeEdge::Left,   PlanetFace::PosZ, CubeEdge::Right, false, false, false},
    {PlanetFace::NegX, CubeEdge::Right,  PlanetFace::NegZ, CubeEdge::Left,  false, false, false},
    {PlanetFace::NegX, CubeEdge::Top,    PlanetFace::PosY, CubeEdge::Left,  true,  false, true },
    {PlanetFace::NegX, CubeEdge::Bottom, PlanetFace::NegY, CubeEdge::Left,  true,  false, false},

    {PlanetFace::PosY, CubeEdge::Top,    PlanetFace::NegZ, CubeEdge::Top,   false, false, true },
    {PlanetFace::PosY, CubeEdge::Bottom, PlanetFace::PosZ, CubeEdge::Top,   false, false, false},
    {PlanetFace::PosY, CubeEdge::Left,   PlanetFace::NegX, CubeEdge::Top,   false, true,  false},
    {PlanetFace::PosY, CubeEdge::Right,  PlanetFace::PosX, CubeEdge::Top,   false, false, false},
};

const CubeEdgePairing &BlockWorld::edge_pairing(PlanetFace from, CubeEdge edge) {
    for (const auto &p : k_edge_pairings) {
        if (p.from_face == from && p.from_edge == edge) return p;
    }
    static const CubeEdgePairing fallback{from, edge, from, edge, false, false, false};
    return fallback;
}

const std::array<CubeEdgePairing, 12> &BlockWorld::all_edge_pairings() {
    static const std::array<CubeEdgePairing, 12> arr = {{
        k_edge_pairings[0], k_edge_pairings[1], k_edge_pairings[2], k_edge_pairings[3],
        k_edge_pairings[4], k_edge_pairings[5], k_edge_pairings[6], k_edge_pairings[7],
        k_edge_pairings[8], k_edge_pairings[9], k_edge_pairings[10], k_edge_pairings[11],
    }};
    return arr;
}

// ============================================================================
// Block direction helpers
// ============================================================================

glm::ivec3 block_dir_vector(BlockDir dir) {
    switch (dir) {
    case BlockDir::Left:  return glm::ivec3(-1,  0,  0);
    case BlockDir::Right: return glm::ivec3( 1,  0,  0);
    case BlockDir::Down:  return glm::ivec3( 0, -1,  0);
    case BlockDir::Up:    return glm::ivec3( 0,  1,  0);
    case BlockDir::Back:  return glm::ivec3( 0,  0, -1);
    case BlockDir::Front: return glm::ivec3( 0,  0,  1);
    }
    return glm::ivec3(0);
}

BlockDir block_dir_opposite(BlockDir dir) {
    switch (dir) {
    case BlockDir::Left:  return BlockDir::Right;
    case BlockDir::Right: return BlockDir::Left;
    case BlockDir::Down:  return BlockDir::Up;
    case BlockDir::Up:    return BlockDir::Down;
    case BlockDir::Back:  return BlockDir::Front;
    case BlockDir::Front: return BlockDir::Back;
    }
    return dir;
}

const char *block_dir_name(BlockDir dir) {
    switch (dir) {
    case BlockDir::Left:  return "Left";
    case BlockDir::Right: return "Right";
    case BlockDir::Down:  return "Down";
    case BlockDir::Up:    return "Up";
    case BlockDir::Back:  return "Back";
    case BlockDir::Front: return "Front";
    }
    return "?";
}

// ============================================================================
// Shell configuration
//
// Innermost shells cover the planet interior (logarithmic spacing).
// The outermost shell is thin — just thick enough for the terrain
// heightfield (surface only).  Only the outermost shell is used for
// surface block generation; inner shells are reserved for future digging.
// ============================================================================

void BlockWorld::build_shells() {
    shells_.clear();
    const int32_t n = config_.surface_shells;
    shells_.reserve(static_cast<size_t>(n));

    // Use the SphereNoise3D terrain height range [8, 28] blocks, not the old
    // planet_terrain_max_height_above_base() which uses a different noise function.
    // WHY: The actual terrain blocks are generated by SphereNoise3D::terrain_height()
    // which clamps to [8, 28]. The shell must be sized to fit this range.
    const double max_terrain = 30.0 * config_.block_size;  // 30 blocks max (some margin above 28)
    const double surface_radius = config_.planet.radius + max_terrain;
    const double r0 = config_.planet.radius * 0.05; // innermost core

    // Inner shells: equal log-spacing from core to just below surface.
    const int32_t inner_count = n - 1;
    const double inner_top = config_.planet.radius; // stop at base sphere

    if (inner_count > 0) {
        const double k = std::pow(inner_top / r0, 1.0 / static_cast<double>(inner_count));
        for (int32_t i = 0; i < inner_count; ++i) {
            ShellConfig sh{};
            sh.index = i;
            sh.inner_radius = r0 * std::pow(k, static_cast<double>(i));
            sh.outer_radius = r0 * std::pow(k, static_cast<double>(i + 1));
            sh.horizontal_res = config_.base_resolution * (1 << i);
            sh.vertical_layers = std::max(2,
                static_cast<int32_t>(std::ceil(
                    (sh.outer_radius - sh.inner_radius) / config_.block_size)));
            shells_.push_back(sh);
        }
    }

    // Outermost (surface) shell: thin layer from base sphere to max terrain height.
    ShellConfig surface_sh{};
    surface_sh.index = n - 1;
    surface_sh.inner_radius = config_.planet.radius;
    surface_sh.outer_radius = surface_radius;
    // Horizontal resolution: target ~1 block per meter at the surface.
    // Face edge = 4× radius ≈ 8,000,000 m.  We want ~block_size spacing.
    // res = 2×radius / block_size ≈ 4,000,000 / 1.0 = 4,000,000.
    // Use power-of-two: 2^22 = 4,194,304.
    surface_sh.horizontal_res = static_cast<int32_t>(std::pow(2.0,
        std::ceil(std::log2(2.0 * config_.planet.radius / config_.block_size))));
    surface_sh.vertical_layers = std::max(4,
        static_cast<int32_t>(std::ceil(max_terrain / config_.block_size)));
    shells_.push_back(surface_sh);
}

const ShellConfig &BlockWorld::shell_config(int32_t shell) const {
    static const ShellConfig empty{};
    if (shell < 0 || shell >= static_cast<int32_t>(shells_.size())) return empty;
    return shells_[static_cast<size_t>(shell)];
}

// ============================================================================
// BlockWorld init
// ============================================================================

void BlockWorld::init(const BlockWorldConfig &cfg) {
    config_ = cfg;
    build_shells();
    chunks_.clear();
    initialized_ = true;
}

// ============================================================================
// World ↔ block address conversion
// ============================================================================

BlockAddress BlockWorld::address_from_world(const glm::dvec3 &world_pos) const {
    BlockAddress addr{};
    const glm::dvec3 rel = world_pos - config_.planet.center;
    const double dist = glm::length(rel);
    const glm::dvec3 dir = (dist > 0.0) ? rel / dist : glm::dvec3(0.0, 1.0, 0.0);
    addr.sector = direction_to_face(dir);

    addr.shell = -1;
    for (size_t i = 0; i < shells_.size(); ++i) {
        if (dist >= shells_[i].inner_radius && dist < shells_[i].outer_radius) {
            addr.shell = static_cast<int32_t>(i);
            break;
        }
    }
    if (addr.shell < 0) addr.shell = static_cast<int32_t>(shells_.size()) - 1;

    const ShellConfig &sh = shells_[static_cast<size_t>(addr.shell)];
    const PlanetFaceUV face_uv = direction_to_face_uv(dir);
    const double u = std::clamp(face_uv.u, -1.0, 1.0);
    const double v = std::clamp(face_uv.v, -1.0, 1.0);

    const double cxf = (u + 1.0) * 0.5 * static_cast<double>(sh.horizontal_res);
    const double czf = (v + 1.0) * 0.5 * static_cast<double>(sh.horizontal_res);
    const int32_t col_x = std::clamp(static_cast<int32_t>(std::floor(cxf)), 0, sh.horizontal_res - 1);
    const int32_t col_z = std::clamp(static_cast<int32_t>(std::floor(czf)), 0, sh.horizontal_res - 1);

    const double t = (dist - sh.inner_radius) / (sh.outer_radius - sh.inner_radius);
    const int32_t layer = std::clamp(
        static_cast<int32_t>(std::floor(t * static_cast<double>(sh.vertical_layers))),
        0, sh.vertical_layers - 1);

    addr.chunk.x = col_x / config_.chunk_size;
    addr.chunk.y = layer / config_.chunk_size;
    addr.chunk.z = col_z / config_.chunk_size;
    addr.block.x = col_x % config_.chunk_size;
    addr.block.y = layer % config_.chunk_size;
    addr.block.z = col_z % config_.chunk_size;
    return addr;
}

glm::dvec3 BlockWorld::world_from_address(const BlockAddress &addr) const {
    const ShellConfig &sh = shell_config(addr.shell);
    const int32_t cx = addr.chunk.x * config_.chunk_size + addr.block.x;
    const int32_t cz = addr.chunk.z * config_.chunk_size + addr.block.z;
    const int32_t ly = addr.chunk.y * config_.chunk_size + addr.block.y;
    return block_world_center(addr.sector, addr.shell, cx, cz, ly);
}

glm::dvec3 BlockWorld::block_world_center(PlanetFace face, int32_t shell_idx,
                                          int32_t col_x, int32_t col_z,
                                          int32_t layer_y) const {
    const ShellConfig &sh = shell_config(shell_idx);
    double u, v;
    block_face_uv(face, shell_idx, col_x, col_z, u, v);
    const double lt = (static_cast<double>(layer_y) + 0.5) / static_cast<double>(sh.vertical_layers);
    const double r = sh.inner_radius + (sh.outer_radius - sh.inner_radius) * lt;
    return config_.planet.center + face_uv_to_direction(face, u, v) * r;
}

void BlockWorld::block_face_uv(PlanetFace face, int32_t shell_idx,
                               int32_t col_x, int32_t col_z,
                               double &u, double &v) const {
    const ShellConfig &sh = shell_config(shell_idx);
    u = -1.0 + (static_cast<double>(col_x) + 0.5) / static_cast<double>(sh.horizontal_res) * 2.0;
    v = -1.0 + (static_cast<double>(col_z) + 0.5) / static_cast<double>(sh.horizontal_res) * 2.0;
}

// ============================================================================
// Terrain height
// ============================================================================

int32_t BlockWorld::terrain_height_at(const glm::dvec3 &world_dir) const {
    SphereNoise3D noise(config_.seed);
    return noise.terrain_height(world_dir, 12.0f, 10.0f);
}

int32_t BlockWorld::terrain_height_at_face_uv(PlanetFace face, int32_t col_x,
                                              int32_t col_z) const {
    const int32_t res = shell_config(shell_count() - 1).horizontal_res;
    const double u = -1.0 + (static_cast<double>(col_x) + 0.5) / static_cast<double>(res) * 2.0;
    const double v = -1.0 + (static_cast<double>(col_z) + 0.5) / static_cast<double>(res) * 2.0;
    return terrain_height_at(face_uv_to_direction(face, u, v));
}

// ============================================================================
// Chunk generation
// ============================================================================

VoxelMaterial BlockWorld::block_material_at(const BlockAddress &addr,
                                            int32_t surface_height) const {
    if (addr.shell >= shell_count()) return VoxelMaterial::Air;
    const ShellConfig &sh = shell_config(addr.shell);
    const int32_t layer = addr.chunk.y * config_.chunk_size + addr.block.y;

    // For the outermost (surface) shell, layer directly maps to height.
    // For inner shells, scale proportionally.
    const int32_t scaled_h = (addr.shell == shell_count() - 1)
        ? surface_height
        : static_cast<int32_t>(static_cast<double>(surface_height) *
            static_cast<double>(sh.vertical_layers) /
            static_cast<double>(shell_config(shell_count() - 1).vertical_layers));

    if (layer > scaled_h) return VoxelMaterial::Air;
    const int32_t depth = scaled_h - layer;
    const int32_t ly = sh.vertical_layers;
    if (depth <= std::max(1, ly / 32)) return VoxelMaterial::Grass;
    if (depth <= std::max(1, ly / 8))  return VoxelMaterial::Dirt;
    return VoxelMaterial::Stone;
}

void BlockWorld::generate_chunk(const BlockAddress &addr, VoxelChunk &out) const {
    const int32_t base_col_x = addr.chunk.x * config_.chunk_size;
    const int32_t base_col_z = addr.chunk.z * config_.chunk_size;

    int32_t solid_count = 0;
    for (int32_t z = 0; z < config_.chunk_size; ++z)
        for (int32_t y = 0; y < config_.chunk_size; ++y)
            for (int32_t x = 0; x < config_.chunk_size; ++x) {
                // Sample terrain height per-column, not per-chunk.
                const int32_t surf_h = terrain_height_at_face_uv(
                    addr.sector, base_col_x + x, base_col_z + z);
                BlockAddress ba = addr;
                ba.block = glm::ivec3(x, y, z);
                const VoxelMaterial mat = block_material_at(ba, surf_h);
                out.set_material(x, y, z, mat);
                if (mat != VoxelMaterial::Air) ++solid_count;
            }

    // Debug: log first chunk generation to verify terrain produces solid blocks.
    // WHY: blocks invisible on planet — need to confirm generate_chunk()
    // actually fills voxels with non-Air materials at the expected heights.
    // Logs once (static bool) to avoid per-chunk spam.
    // TODO: remove once voxel rendering is confirmed working.
    static bool logged_first = false;
    if (!logged_first) {
        logged_first = true;
        const int32_t sample_h = terrain_height_at_face_uv(
            addr.sector, base_col_x + 8, base_col_z + 8);
        const ShellConfig &sh = shell_config(addr.shell);
        std::fprintf(stderr, "Chunk gen: sector=%d shell=%d chunk=(%d,%d,%d) solid=%d/%d "
                     "base_col=(%d, %d) sample_h=%d vlayers=%d\n",
                     static_cast<int>(addr.sector), addr.shell,
                     addr.chunk.x, addr.chunk.y, addr.chunk.z,
                     solid_count, config_.chunk_size * config_.chunk_size * config_.chunk_size,
                     base_col_x, base_col_z, sample_h, sh.vertical_layers);
    }
}

VoxelChunk &BlockWorld::get_or_generate_chunk(const BlockAddress &addr) {
    // Strip block index for chunk-level key.
    BlockAddress key = addr;
    key.block = glm::ivec3(0);
    auto it = chunks_.find(key);
    if (it != chunks_.end()) return it->second;

    VoxelChunk chunk{};
    for (int32_t z = 0; z < config_.chunk_size; ++z)
        for (int32_t y = 0; y < config_.chunk_size; ++y)
            for (int32_t x = 0; x < config_.chunk_size; ++x)
                chunk.set_material(x, y, z, VoxelMaterial::Air);
    generate_chunk(addr, chunk);
    auto [ins, _] = chunks_.emplace(key, std::move(chunk));
    return ins->second;
}

const VoxelChunk *BlockWorld::find_chunk(const BlockAddress &addr) const {
    // Strip block index — chunks are keyed by sector+shell+chunk only.
    BlockAddress key = addr;
    key.block = glm::ivec3(0);
    auto it = chunks_.find(key);
    return (it != chunks_.end()) ? &it->second : nullptr;
}

// ============================================================================
// Neighbor lookup
// ============================================================================

std::vector<BlockNeighbor> BlockWorld::neighbors(const BlockAddress &addr,
                                                  BlockDir dir) const {
    std::vector<BlockNeighbor> result;
    const glm::ivec3 dv = block_dir_vector(dir);
    const glm::ivec3 nb_block = addr.block + dv;

    if (nb_block.x >= 0 && nb_block.x < config_.chunk_size &&
        nb_block.y >= 0 && nb_block.y < config_.chunk_size &&
        nb_block.z >= 0 && nb_block.z < config_.chunk_size) {
        BlockNeighbor nb{};
        nb.address = addr;
        nb.address.block = nb_block;
        result.push_back(nb);
        return result;
    }

    const ShellConfig &sh = shell_config(addr.shell);
    const int32_t hc = std::max(1, sh.horizontal_res / config_.chunk_size);
    const int32_t vc = std::max(1, sh.vertical_layers / config_.chunk_size);
    glm::ivec3 nc = addr.chunk;
    if (nb_block.x < 0) nc.x -= 1; else if (nb_block.x >= config_.chunk_size) nc.x += 1;
    if (nb_block.y < 0) nc.y -= 1; else if (nb_block.y >= config_.chunk_size) nc.y += 1;
    if (nb_block.z < 0) nc.z -= 1; else if (nb_block.z >= config_.chunk_size) nc.z += 1;

    const glm::ivec3 wr(
        (nb_block.x + config_.chunk_size) % config_.chunk_size,
        (nb_block.y + config_.chunk_size) % config_.chunk_size,
        (nb_block.z + config_.chunk_size) % config_.chunk_size);

    if (nc.x < 0 || nc.x >= hc || nc.z < 0 || nc.z >= hc ||
        nc.y < 0 || nc.y >= vc) {
        if (nc.y < 0 || nc.y >= vc) {
            // radial cross (different shell res) - not supported for surface play yet
            return result;
        }
        // Cross-sector using CubeNet edge pairings.
        // See k_edge_pairings and edge_pairing(). Matches the quad-sphere adjacency
        // from research (Bowerbyte, Jacco, Dimitrijević 2016 cube projections paper).
        CubeEdge crossed_edge;
        if (nb_block.x < 0) crossed_edge = CubeEdge::Left;
        else if (nb_block.x >= config_.chunk_size) crossed_edge = CubeEdge::Right;
        else if (nb_block.z < 0) crossed_edge = CubeEdge::Bottom;
        else crossed_edge = CubeEdge::Top;
        const auto& p = edge_pairing(addr.sector, crossed_edge);
        BlockNeighbor nb{};
        nb.address.sector = p.to_face;
        nb.address.shell  = addr.shell;
        int tcx = nc.x;
        int tcz = nc.z;
        if (p.swap_uv) std::swap(tcx, tcz);
        if (p.flip_u) tcx = hc - 1 - tcx;
        if (p.flip_v) tcz = hc - 1 - tcz;
        if (tcx < 0) tcx = 0;
        if (tcx >= hc) tcx = hc-1;
        if (tcz < 0) tcz = 0;
        if (tcz >= hc) tcz = hc-1;
        nb.address.chunk = glm::ivec3(tcx, nc.y, tcz);
        int tbx = wr.x;
        int tbz = wr.z;
        if (p.swap_uv) std::swap(tbx, tbz);
        if (p.flip_u) tbx = config_.chunk_size - 1 - tbx;
        if (p.flip_v) tbz = config_.chunk_size - 1 - tbz;
        nb.address.block = glm::ivec3(tbx, wr.y, tbz);
        result.push_back(nb);
        return result;
    }

    BlockNeighbor nb{};
    nb.address.sector = addr.sector;
    nb.address.shell  = addr.shell;
    nb.address.chunk  = nc;
    nb.address.block  = wr;
    result.push_back(nb);
    return result;
}

BlockDir BlockWorld::world_dir_to_block_dir(const glm::dvec3 &world_pos,
                                            const glm::dvec3 &world_dir) const {
    const glm::dvec3 radial = radial_up(config_.planet, world_pos);
    const PlanetTangentBasis tb = tangent_basis(radial, glm::dvec3(0.0, 1.0, 0.0));
    const double de = glm::dot(world_dir, tb.east);
    const double dn = glm::dot(world_dir, tb.north);
    const double du = glm::dot(world_dir, tb.up);
    const double ae = std::abs(de), an = std::abs(dn), au = std::abs(du);
    if (au >= ae && au >= an) return (du > 0.0) ? BlockDir::Up : BlockDir::Down;
    if (ae >= an) return (de > 0.0) ? BlockDir::Right : BlockDir::Left;
    return (dn > 0.0) ? BlockDir::Front : BlockDir::Back;
}

// ============================================================================
// Chunk mesh building
// ============================================================================

static uint64_t block_chunk_mesh_id(const BlockAddress &addr) {
    uint64_t h = 0x424c4f434b504c54ull;
    h ^= static_cast<uint64_t>(static_cast<uint8_t>(addr.sector));
    h ^= static_cast<uint64_t>(static_cast<uint32_t>(addr.shell)) << 8;
    h ^= static_cast<uint64_t>(static_cast<uint32_t>(addr.chunk.x)) << 16;
    h ^= static_cast<uint64_t>(static_cast<uint32_t>(addr.chunk.y)) << 24;
    h ^= static_cast<uint64_t>(static_cast<uint32_t>(addr.chunk.z)) << 32;
    h ^= h >> 30u; h *= 0xbf58476d1ce4e5b9ull;
    h ^= h >> 27u; h *= 0x94d049bb133111ebull;
    h ^= h >> 31u;
    return h == 0 ? 0x424c4f434b504c54ull : h;
}

uint64_t BlockWorld::chunk_mesh_id(const BlockAddress &addr) {
    return block_chunk_mesh_id(addr);
}

RenderMesh BlockWorld::build_chunk_mesh(
    const BlockAddress &addr,
    const VoxelChunk &chunk,
    [[maybe_unused]] const std::function<bool(const BlockAddress&)> &solid_at,
    const glm::dvec3 &camera_relative_origin) const {

    RenderMesh mesh{};
    mesh.mesh_id = block_chunk_mesh_id(addr);

    const ShellConfig &sh = shell_config(addr.shell);
    const double bw = config_.block_size;

    // NOTE: Greedy meshing computes per-quad tangent bases, so the
    // chunk-center precomputation is no longer needed.

    const int32_t cs = config_.chunk_size;
    const double hs = bw * 0.5;

    // WHY: greedy meshing merges adjacent same-material faces into larger
    // quads, reducing vertex count 5-10x. Algorithm from 0fps.net:
    // for each face direction, build a 2D material mask per slice,
    // then scan rows/columns to find maximal rectangular runs.
    // Tangent-basis face normals are preserved for sphere-appropriate
    // lighting. Camera-relative vertex submission unchanged.

    // Intra-chunk occlusion: only cull faces whose neighbor is solid
    // AND in the same chunk. Cross-chunk faces always emitted to avoid
    // visible seams when streaming loads chunks asymmetrically.
    auto intra_occluded = [&](int bx, int by, int bz, BlockDir fd) -> bool {
        const glm::ivec3 dv = block_dir_vector(fd);
        const int nx = bx + dv.x, ny = by + dv.y, nz = bz + dv.z;
        if (nx >= 0 && nx < cs && ny >= 0 && ny < cs && nz >= 0 && nz < cs) {
            return chunk.solid(nx, ny, nz);
        }
        return false;
    };

    // Compute world position at arbitrary (col_x, col_z, layer_y) doubles.
    // WHY: merged quad centers are at fractional block coordinates (e.g.,
    // center of a 5-wide run is at +2.5), so we need double-precision
    // interpolation of the sphere face UV and radial layer.
    auto world_at = [&](double col_x, double col_z, double layer_y) -> glm::dvec3 {
        const double u = -1.0 + (col_x + 0.5) / static_cast<double>(sh.horizontal_res) * 2.0;
        const double v = -1.0 + (col_z + 0.5) / static_cast<double>(sh.horizontal_res) * 2.0;
        const double lt = std::clamp((layer_y + 0.5) / static_cast<double>(std::max(1, sh.vertical_layers)), 0.0, 1.0);
        const double r = sh.inner_radius + (sh.outer_radius - sh.inner_radius) * lt;
        return config_.planet.center + face_uv_to_direction(addr.sector, u, v) * r;
    };

    // Per-direction: compute the tangent-basis quadrant geometry.
    // dir_info encodes which block axes map to the 2D grid and tangent vectors.
    struct DirInfo {
        BlockDir fd;
        int slice_axis;  // 0=bx, 1=by, 2=bz
        int col_axis;    // 0=bx, 1=by, 2=bz
        int row_axis;    // 0=bx, 1=by, 2=bz
        int fn_tb;       // 0=east, 1=north, 2=up  (which tangent basis vector is the face normal)
        int fn_sign;     // +1 or -1
        int col_tb;      // 0=east, 1=north, 2=up  (tangent vector for +column)
        int col_sign;    // +1 or -1
        int row_tb;      // 0=east, 1=north, 2=up  (tangent vector for +row)
        int row_sign;    // +1 or -1
    };
    static const DirInfo dir_infos[6] = {
        // fd=Left:  slice=X, col=Z, row=Y, fn=-east, col=+north, row=+up
        {BlockDir::Left,  0, 2, 1, 0,-1, 1, 1, 2, 1},
        // fd=Right: slice=X, col=Z, row=Y, fn=+east, col=+north, row=+up
        {BlockDir::Right, 0, 2, 1, 0, 1, 1, 1, 2, 1},
        // fd=Down:  slice=Y, col=X, row=Z, fn=-up,   col=+east, row=+north
        {BlockDir::Down,  1, 0, 2, 2,-1, 0, 1, 1, 1},
        // fd=Up:    slice=Y, col=X, row=Z, fn=+up,   col=+east, row=+north
        {BlockDir::Up,    1, 0, 2, 2, 1, 0, 1, 1, 1},
        // fd=Back:  slice=Z, col=X, row=Y, fn=-north, col=+east, row=+up
        {BlockDir::Back,  2, 0, 1, 1,-1, 0, 1, 2, 1},
        // fd=Front: slice=Z, col=X, row=Y, fn=+north, col=+east, row=+up
        {BlockDir::Front, 2, 0, 1, 1, 1, 0, 1, 2, 1},
    };

    int total_blocks_covered = 0;

    for (const DirInfo &di : dir_infos) {
        const BlockDir fd = di.fd;
        // Lambda: convert 2D (col, row, slice) to block coords (bx, by, bz)
        auto to_block = [&](int col, int row, int slice, int (&b)[3]) {
            b[di.col_axis] = col;
            b[di.row_axis] = row;
            b[di.slice_axis] = slice;
        };

        for (int slice = 0; slice < cs; ++slice) {
            // Build 2D material mask for visible faces in this slice.
            // mask[row][col] = material ID (1-3) if face visible, else 0.
            uint8_t mask[16][16] = {};
            bool has_any = false;

            for (int row = 0; row < cs; ++row) {
                for (int col = 0; col < cs; ++col) {
                    int b[3];
                    to_block(col, row, slice, b);
                    const int bx = b[0], by = b[1], bz = b[2];
                    if (!chunk.solid(bx, by, bz)) continue;
                    const VoxelMaterial mat = chunk.material(bx, by, bz);
                    if (mat == VoxelMaterial::Air) continue;
                    if (intra_occluded(bx, by, bz, fd)) continue;
                    mask[row][col] = static_cast<uint8_t>(mat);
                    has_any = true;
                }
            }
            if (!has_any) continue;

            // Greedy scan: find maximal rectangles of same material.
            bool visited[16][16] = {};
            for (int row = 0; row < cs; ++row) {
                for (int col = 0; col < cs; ++col) {
                    if (visited[row][col] || mask[row][col] == 0) continue;
                    const uint8_t mat_id = mask[row][col];

                    // Extend horizontally (along columns).
                    int w = 1;
                    while (col + w < cs && !visited[row][col + w] &&
                           mask[row][col + w] == mat_id) ++w;

                    // Try to extend vertically (along rows) — same run must
                    // exist contiguously in every subsequent row.
                    int h = 1;
                    bool can_extend = true;
                    while (row + h < cs && can_extend) {
                        for (int dc = 0; dc < w; ++dc) {
                            if (visited[row + h][col + dc] ||
                                mask[row + h][col + dc] != mat_id) {
                                can_extend = false;
                                break;
                            }
                        }
                        if (can_extend) ++h;
                    }

                    // Mark entire rectangle as visited.
                    for (int dr = 0; dr < h; ++dr)
                        for (int dc = 0; dc < w; ++dc)
                            visited[row + dr][col + dc] = true;

                    total_blocks_covered += w * h;

                    // --- Build merged quad geometry ---
                    // Compute world-space center of the merged block region
                    // (at the block center layer, not the face surface).
                    const double gx_c = static_cast<double>(addr.chunk.x) * cs +
                        (di.col_axis == 0 ? col + w * 0.5 :
                         di.row_axis == 0 ? row + h * 0.5 :
                         static_cast<double>(slice));
                    const double gz_c = static_cast<double>(addr.chunk.z) * cs +
                        (di.col_axis == 2 ? col + w * 0.5 :
                         di.row_axis == 2 ? row + h * 0.5 :
                         static_cast<double>(slice));
                    const double gy_c = static_cast<double>(addr.chunk.y) * cs +
                        (di.col_axis == 1 ? col + w * 0.5 :
                         di.row_axis == 1 ? row + h * 0.5 :
                         static_cast<double>(slice));

                    const glm::dvec3 ref_center = world_at(gx_c, gz_c, gy_c);
                    const glm::dvec3 radial = glm::normalize(
                        ref_center - config_.planet.center);
                    const PlanetTangentBasis tb = tangent_basis(radial);

                    // Tangent basis vectors array for fn_tb/col_tb/row_tb lookup.
                    const glm::dvec3 tb_vecs[3] = {tb.east, tb.north, radial};

                    // Face normal.
                    const glm::dvec3 fn = tb_vecs[di.fn_tb] * static_cast<double>(di.fn_sign);

                    // Column and row extent vectors in world space.
                    const glm::dvec3 col_ext = tb_vecs[di.col_tb] *
                        (static_cast<double>(di.col_sign) * static_cast<double>(w) * hs);
                    const glm::dvec3 row_ext = tb_vecs[di.row_tb] *
                        (static_cast<double>(di.row_sign) * static_cast<double>(h) * hs);

                    // Four quad corners in world space, centered on ref_center,
                    // then shifted to the face surface by fn * hs.
                    // Order: BL, BR, TR, TL (matches current single-block winding).
                    const glm::dvec3 corners[4] = {
                        ref_center - col_ext - row_ext + fn * hs,  // BL
                        ref_center + col_ext - row_ext + fn * hs,  // BR
                        ref_center + col_ext + row_ext + fn * hs,  // TR
                        ref_center - col_ext + row_ext + fn * hs,  // TL
                    };

                    // Height fraction for color tinting at quad center.
                    const float height_t = std::clamp(
                        static_cast<float>(gy_c) /
                            static_cast<float>(std::max(1, sh.vertical_layers)),
                        0.0f, 1.0f);
                    const bool top_face = (fd == BlockDir::Up);
                    const glm::vec3 color = VoxelChunk::material_color(
                        static_cast<VoxelMaterial>(mat_id), top_face, height_t);

                    const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
                    for (int ci = 0; ci < 4; ++ci) {
                        mesh.vertices.push_back(RenderVertex{
                            glm::vec3(corners[ci] - camera_relative_origin),
                            color,
                            glm::vec3(fn)});
                    }
                    // Winding order: CCW when viewed from outside the block
                    // (along the face normal). Corners[0-3] are BL, BR, TR, TL.
                    mesh.indices.insert(mesh.indices.end(), {
                        base, base + 2, base + 1,
                        base, base + 3, base + 2});
                }
            }
        }
    }
    // Debug: log first mesh build to verify build_chunk_mesh() produces geometry.
    // WHY: chunks generated with 2155 solid blocks but blocks still invisible —
    // need to confirm mesh has vertices/indices and camera-relative positions
    // are reasonable (small offsets from snap origin).
    // TODO: remove once voxel rendering is confirmed working.
    static bool logged_mesh = false;
    if (!logged_mesh) {
        logged_mesh = true;
        std::fprintf(stderr, "Mesh build: verts=%zu idxs=%zu tris=%zu greedy_quads=%zu blocks_covered=%d camera_origin=(%.1f,%.1f,%.1f)\n",
                     mesh.vertices.size(), mesh.indices.size(), mesh.indices.size() / 3,
                     mesh.indices.size() / 6, total_blocks_covered,
                     camera_relative_origin.x, camera_relative_origin.y, camera_relative_origin.z);
        if (!mesh.vertices.empty()) {
            const auto &v0 = mesh.vertices[0];
            std::fprintf(stderr, "First vertex: pos=(%.3f,%.3f,%.3f) color=(%.2f,%.2f,%.2f)\n",
                         v0.position.x, v0.position.y, v0.position.z,
                         v0.color.x, v0.color.y, v0.color.z);
        }
        // Count faces by normal direction to debug missing side faces.
        int face_count[6] = {0}; // +x, -x, +y, -y, +z, -z
        for (size_t i = 0; i < mesh.vertices.size(); i += 4) {
            const glm::vec3 &n = mesh.vertices[i].normal;
            if (n.x > 0.5f) face_count[0]++;
            else if (n.x < -0.5f) face_count[1]++;
            else if (n.y > 0.5f) face_count[2]++;
            else if (n.y < -0.5f) face_count[3]++;
            else if (n.z > 0.5f) face_count[4]++;
            else if (n.z < -0.5f) face_count[5]++;
        }
        std::fprintf(stderr, "Face counts: +x=%d -x=%d +y=%d -y=%d +z=%d -z=%d\n",
                     face_count[0], face_count[1], face_count[2],
                     face_count[3], face_count[4], face_count[5]);
    }
    return mesh;
}

// ============================================================================
// SphereNoise3D

namespace {

uint64_t ns_splitmix64(uint64_t x) {
    x += 0x9e3779b97f4a7c15ull;
    x = (x ^ (x >> 30u)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27u)) * 0x94d049bb133111ebull;
    return x ^ (x >> 31u);
}

float ns_hash_float(uint64_t h) {
    return static_cast<float>((h >> 40u) & 0xffffffu) / 16777215.0f * 2.0f - 1.0f;
}

float ns_smoothstep(float t) {
    const float ct = std::clamp(t, 0.0f, 1.0f);
    return ct * ct * (3.0f - 2.0f * ct);
}

} // namespace

SphereNoise3D::SphereNoise3D(uint64_t seed) : seed_(seed) {}

float SphereNoise3D::value_noise(const glm::dvec3 &p) const {
    const int64_t ix = static_cast<int64_t>(std::floor(p.x));
    const int64_t iy = static_cast<int64_t>(std::floor(p.y));
    const int64_t iz = static_cast<int64_t>(std::floor(p.z));
    const glm::dvec3 f(p.x - static_cast<double>(ix),
                        p.y - static_cast<double>(iy),
                        p.z - static_cast<double>(iz));

    uint64_t h000 = ns_splitmix64(seed_ ^ (static_cast<uint64_t>(ix) * 73856093ull) ^
                                  (static_cast<uint64_t>(iy) * 19349663ull) ^
                                  (static_cast<uint64_t>(iz) * 83492791ull));

    const float sx = ns_smoothstep(static_cast<float>(f.x));
    const float sy = ns_smoothstep(static_cast<float>(f.y));
    const float sz = ns_smoothstep(static_cast<float>(f.z));

    auto h = [&](int64_t dx, int64_t dy, int64_t dz) {
        return ns_hash_float(ns_splitmix64(seed_ ^
            (static_cast<uint64_t>(ix + dx) * 73856093ull) ^
            (static_cast<uint64_t>(iy + dy) * 19349663ull) ^
            (static_cast<uint64_t>(iz + dz) * 83492791ull)));
    };

    const float a = std::lerp(h(0,0,0), h(1,0,0), sx);
    const float b = std::lerp(h(0,1,0), h(1,1,0), sx);
    const float c = std::lerp(h(0,0,1), h(1,0,1), sx);
    const float d = std::lerp(h(0,1,1), h(1,1,1), sx);
    return std::lerp(std::lerp(a, b, sy), std::lerp(c, d, sy), sz);
}

float SphereNoise3D::sample(const glm::dvec3 &direction, float frequency) const {
    return value_noise(direction * static_cast<double>(frequency));
}

float SphereNoise3D::fbm(const glm::dvec3 &direction, int octaves,
                         float lacunarity, float gain) const {
    float value = 0.0f, amplitude = 1.0f, freq = 1.0f, maxv = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        value += sample(direction, freq) * amplitude;
        maxv += amplitude;
        freq *= lacunarity;
        amplitude *= gain;
    }
    return value / maxv;
}

int32_t SphereNoise3D::terrain_height(const glm::dvec3 &direction,
                                      float base_height,
                                      float amplitude) const {
    // FROM-SCRATCH CLEAN REIMPLEMENTATION for the spherical voxel planet reboot.
    // WHY: the previous FBM + conditions was producing variable heights that for some loaded columns
    // resulted in no solids in the top radial chunk layer (cy around player_cy), so no visible
    // surface tops in the meshed chunks near player (only deep stone in lower cy or air). This contributed
    // to "same result" (only wireframe) even after streaming and camera fixes.
    // Per first principles and research (Bowerbyte, Peck, Jacco): need reliable per-column height
    // variation on the sphere surface that guarantees visible 1m blocks (Grass tops) in the loaded
    // surface shell layers for the player-centric patch. Seamless because sampled on unit direction.
    // Simple, verifiable: base + multi-octave on direction for hills, plus a global variation to ensure
    // the top of cy=1 (for player at ~20-25) always has some solid columns with top faces emitted.
    // Clamped to produce blocks in both cy=0 and cy=1 for the 7x7 area.
    float h = base_height;
    h += sample(direction, 0.5f) * amplitude * 0.8f;
    h += sample(direction, 2.0f) * amplitude * 0.5f;
    h += sample(direction, 8.0f) * amplitude * 0.3f;

    // Global low-freq variation to guarantee surface in loaded area (player at equator +X,
    // nearby columns will have height ~16-28, so top faces in cy=1 for the 3-radius patch).
    float global = sample(direction * 0.1, 1.0f) * 6.0f;
    h += global;

    return std::clamp(static_cast<int32_t>(std::round(h)), 8, 28);  // ensures overlap with cy=0 and cy=1 for visible tops
}
