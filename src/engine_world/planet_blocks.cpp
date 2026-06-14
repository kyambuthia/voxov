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
        if (p.to_face == from && p.to_edge == edge) {
            static thread_local CubeEdgePairing rev;
            rev.from_face = p.to_face;
            rev.from_edge = p.to_edge;
            rev.to_face = p.from_face;
            rev.to_edge = p.from_edge;
            rev.swap_uv = p.swap_uv;
            rev.flip_u = p.swap_uv ? p.flip_v : p.flip_u;
            rev.flip_v = p.swap_uv ? p.flip_u : p.flip_v;
            return rev;
        }
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
    // Increased amplitude from 10 to 25 for more visible terrain variation.
    // Base height 15, amplitude 25 gives range [8, 40] clamped to [8, 28].
    return noise.terrain_height(world_dir, 15.0f, 25.0f);
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

    // Debug: log terrain height range in first chunk.
    static bool logged_first = false;
    if (!logged_first) {
        logged_first = true;
        int32_t min_h = 999, max_h = 0;
        for (int32_t z = 0; z < config_.chunk_size; ++z) {
            for (int32_t x = 0; x < config_.chunk_size; ++x) {
                const int32_t h = terrain_height_at_face_uv(
                    addr.sector, base_col_x + x, base_col_z + z);
                min_h = std::min(min_h, h);
                max_h = std::max(max_h, h);
            }
        }
        const ShellConfig &sh = shell_config(addr.shell);
        std::fprintf(stderr, "Chunk gen: sector=%d shell=%d chunk=(%d,%d,%d) solid=%d/%d "
                     "base_col=(%d, %d) terrain_h=[%d..%d] vlayers=%d\n",
                     static_cast<int>(addr.sector), addr.shell,
                     addr.chunk.x, addr.chunk.y, addr.chunk.z,
                     solid_count, config_.chunk_size * config_.chunk_size * config_.chunk_size,
                     base_col_x, base_col_z, min_h, max_h, sh.vertical_layers);
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
    const std::function<bool(const BlockAddress&)> &solid_at,
    const glm::dvec3 &camera_relative_origin,
    int32_t lod_level) const {

    RenderMesh mesh{};
    mesh.mesh_id = block_chunk_mesh_id(addr);

    const ShellConfig &sh = shell_config(addr.shell);
    const double bw = config_.block_size;
    const int32_t cs = config_.chunk_size;
    const double hs = bw * 0.5;

    // LOD stride: at LOD N, only every (2^N)th block is meshed.
    const int32_t stride = 1 << std::clamp(lod_level, 0, 3);

    // WHY per-face meshing instead of greedy meshing:
    // On a sphere, each block has a unique normal pointing away from planet
    // center. Greedy meshing merges adjacent same-material blocks into one
    // quad with a single normal from the quad center, which destroys the
    // spherical curvature and makes terrain look flat. Per-face meshing
    // preserves per-block normals for correct sphere lighting.

    // Intra-chunk face culling: emit face only if neighbor is NOT solid.
    // Cross-chunk face culling: use the solid_at callback to check the
    // neighboring chunk's block. This prevents floating quads at chunk
    // boundaries when adjacent chunks are loaded, while still correctly
    // culling faces against solid neighbors.
    auto should_emit_face = [&](int bx, int by, int bz, BlockDir fd) -> bool {
        const glm::ivec3 dv = block_dir_vector(fd);
        const int nx = bx + dv.x * stride, ny = by + dv.y * stride, nz = bz + dv.z * stride;
        // Same-chunk neighbor: check if solid.
        if (nx >= 0 && nx < cs && ny >= 0 && ny < cs && nz >= 0 && nz < cs) {
            return !chunk.solid(nx, ny, nz);
        }
        
        BlockAddress nb_addr = addr;
        const int hc = std::max(1, sh.horizontal_res / cs);
        const int vc = std::max(1, sh.vertical_layers / cs);

        int cx = addr.chunk.x;
        int cy = addr.chunk.y;
        int cz = addr.chunk.z;

        if (nx < 0) cx -= 1; else if (nx >= cs) cx += 1;
        if (ny < 0) cy -= 1; else if (ny >= cs) cy += 1;
        if (nz < 0) cz -= 1; else if (nz >= cs) cz += 1;

        const int bx_new = (nx % cs + cs) % cs;
        const int by_new = (ny % cs + cs) % cs;
        const int bz_new = (nz % cs + cs) % cs;

        // Cross-chunk neighbor resolution mirrors BlockWorld::neighbors().
        // WHY: For same-face neighbors (cx,cz in bounds), we set chunk+block directly.
        // For cross-sector neighbors (cx,cz out of bounds), we use cube-edge pairings
        // to convert the source chunk+block coordinates onto the adjacent face.
        // Critically, we clamp the out-of-range chunk index to the valid range BEFORE
        // computing block coordinates, so that the edge-pairing flip/swap transforms
        // are applied to valid source-face coordinates — not to garbage negative indices.
        if (cx < 0 || cx >= hc || cz < 0 || cz >= hc || cy < 0 || cy >= vc) {
            if (cy < 0 || cy >= vc) {
                // Radial cross (different shell) — not supported for surface play.
                return true;
            }
            CubeEdge crossed_edge;
            if (cx < 0) crossed_edge = CubeEdge::Left;
            else if (cx >= hc) crossed_edge = CubeEdge::Right;
            else if (cz < 0) crossed_edge = CubeEdge::Bottom;
            else crossed_edge = CubeEdge::Top;

            const auto& p = edge_pairing(addr.sector, crossed_edge);
            nb_addr.sector = p.to_face;

            // Step 1: Clamp out-of-range chunk index to the valid range [0, hc-1].
            // This gives us the chunk on the SOURCE face at the correct edge,
            // which is the boundary neighbor of the adjacent face.
            int scx = std::clamp(cx, 0, hc - 1);
            int scz = std::clamp(cz, 0, hc - 1);

            // Step 2: Apply edge-pairing transforms at the chunk-index level
            // (same approach as BlockWorld::neighbors()).
            if (p.swap_uv) std::swap(scx, scz);
            if (p.flip_u) scx = hc - 1 - scx;
            if (p.flip_v) scz = hc - 1 - scz;

            nb_addr.chunk = glm::ivec3(scx, cy, scz);

            // Step 3: Apply edge-pairing transforms at the block-within-chunk level.
            int tbx = bx_new;
            int tbz = bz_new;
            if (p.swap_uv) std::swap(tbx, tbz);
            if (p.flip_u) tbx = cs - 1 - tbx;
            if (p.flip_v) tbz = cs - 1 - tbz;

            nb_addr.block = glm::ivec3(tbx, by_new, tbz);
        } else {
            nb_addr.chunk = glm::ivec3(cx, cy, cz);
            nb_addr.block = glm::ivec3(bx_new, by_new, bz_new);
        }
        
        return !solid_at(nb_addr);
    };

    struct FaceDef {
        BlockDir fd;
        int corners[4]; // CCW order
    };
    static const FaceDef faces[6] = {
        {BlockDir::Left,  {0, 2, 6, 4}},
        {BlockDir::Right, {1, 5, 7, 3}},
        {BlockDir::Down,  {0, 1, 3, 2}},
        {BlockDir::Up,    {4, 6, 7, 5}},
        {BlockDir::Back,  {0, 4, 5, 1}},
        {BlockDir::Front, {2, 3, 7, 6}}
    };

    // Iterate all blocks in chunk at LOD stride.
    for (int32_t bz = 0; bz < cs; bz += stride) {
        for (int32_t by = 0; by < cs; by += stride) {
            for (int32_t bx = 0; bx < cs; bx += stride) {
                if (!chunk.solid(bx, by, bz)) continue;
                const VoxelMaterial mat = chunk.material(bx, by, bz);
                if (mat == VoxelMaterial::Air) continue;

                // Evaluate 8 corners of the frustum block.
                const double gx0 = static_cast<double>(addr.chunk.x * cs + bx);
                const double gx1 = gx0 + stride;
                const double gy0 = static_cast<double>(addr.chunk.y * cs + by);
                const double gy1 = gy0 + stride;
                const double gz0 = static_cast<double>(addr.chunk.z * cs + bz);
                const double gz1 = gz0 + stride;

                const double res = static_cast<double>(sh.horizontal_res);
                const double vlayers = static_cast<double>(std::max(1, sh.vertical_layers));

                const double u0 = -1.0 + (gx0) / res * 2.0;
                const double u1 = -1.0 + (gx1) / res * 2.0;
                const double v0 = -1.0 + (gz0) / res * 2.0;
                const double v1 = -1.0 + (gz1) / res * 2.0;

                const double r0 = sh.inner_radius + (sh.outer_radius - sh.inner_radius) * (gy0 / vlayers);
                const double r1 = sh.inner_radius + (sh.outer_radius - sh.inner_radius) * (gy1 / vlayers);

                const glm::dvec3 d00 = face_uv_to_direction(addr.sector, u0, v0);
                const glm::dvec3 d10 = face_uv_to_direction(addr.sector, u1, v0);
                const glm::dvec3 d01 = face_uv_to_direction(addr.sector, u0, v1);
                const glm::dvec3 d11 = face_uv_to_direction(addr.sector, u1, v1);

                const glm::dvec3 p[8] = {
                    config_.planet.center + d00 * r0, // 0: 0,0,0
                    config_.planet.center + d10 * r0, // 1: 1,0,0
                    config_.planet.center + d01 * r0, // 2: 0,0,1
                    config_.planet.center + d11 * r0, // 3: 1,0,1
                    config_.planet.center + d00 * r1, // 4: 0,1,0
                    config_.planet.center + d10 * r1, // 5: 1,1,0
                    config_.planet.center + d01 * r1, // 6: 0,1,1
                    config_.planet.center + d11 * r1  // 7: 1,1,1
                };

                // Height fraction for color tinting.
                const float height_t = std::clamp(
                    static_cast<float>(gy0) / static_cast<float>(std::max(1, sh.vertical_layers)),
                    0.0f, 1.0f);

                for (const auto &face : faces) {
                    if (!should_emit_face(bx, by, bz, face.fd)) continue;

                    const bool top_face = (face.fd == BlockDir::Up);
                    const glm::vec3 color = VoxelChunk::material_color(mat, top_face, height_t);

                    const glm::dvec3 v0 = p[face.corners[0]];
                    const glm::dvec3 v1 = p[face.corners[1]];
                    const glm::dvec3 v2 = p[face.corners[2]];
                    const glm::dvec3 v3 = p[face.corners[3]];

                    // Compute exact geometric face normal.
                    glm::dvec3 fn = glm::normalize(glm::cross(v1 - v0, v2 - v0));

                    const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
                    
                    mesh.vertices.push_back(RenderVertex{glm::vec3(v0 - camera_relative_origin), color, glm::vec3(fn)});
                    mesh.vertices.push_back(RenderVertex{glm::vec3(v1 - camera_relative_origin), color, glm::vec3(fn)});
                    mesh.vertices.push_back(RenderVertex{glm::vec3(v2 - camera_relative_origin), color, glm::vec3(fn)});
                    mesh.vertices.push_back(RenderVertex{glm::vec3(v3 - camera_relative_origin), color, glm::vec3(fn)});

                    // CCW winding when viewed from outside (along face normal).
                    mesh.indices.insert(mesh.indices.end(), {
                        base, base + 1, base + 2,
                        base, base + 2, base + 3});
                }
            }
        }
    }

    // Debug: log first mesh build stats.
    static bool logged_mesh = false;
    if (!logged_mesh) {
        logged_mesh = true;
        std::fprintf(stderr, "Mesh build: verts=%zu idxs=%zu tris=%zu camera_origin=(%.1f,%.1f,%.1f)\n",
                     mesh.vertices.size(), mesh.indices.size(), mesh.indices.size() / 3,
                     camera_relative_origin.x, camera_relative_origin.y, camera_relative_origin.z);
        if (!mesh.vertices.empty()) {
            const auto &v0 = mesh.vertices[0];
            std::fprintf(stderr, "First vertex: pos=(%.3f,%.3f,%.3f) color=(%.2f,%.2f,%.2f)\n",
                         v0.position.x, v0.position.y, v0.position.z,
                         v0.color.x, v0.color.y, v0.color.z);
        }
        // Count faces by normal direction.
        int face_count[6] = {0};
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
    // Noise frequencies tuned for per-block variation at 1024 horizontal_res.
    // UV range per 16-block chunk: 16/1024 = 0.0156. Need frequencies high
    // enough to produce multiple cycles within that range.
    // freq=200 → 200*0.0156 = 3.1 cycles per chunk → good variation
    // freq=80  → 80*0.0156 = 1.25 cycles per chunk → medium variation
    // freq=30  → 30*0.0156 = 0.47 cycles per chunk → large hills
    float h = base_height;
    h += sample(direction, 200.0f) * amplitude * 0.5f;  // high-freq detail
    h += sample(direction, 80.0f) * amplitude * 0.3f;   // medium detail
    h += sample(direction, 30.0f) * amplitude * 0.2f;   // large hills

    return std::clamp(static_cast<int32_t>(std::round(h)), 8, 28);
}
