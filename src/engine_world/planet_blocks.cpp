#include "engine_world/planet_blocks.hpp"
#include "engine_world/voxel_chunk.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>    // std::fprintf for debug diagnostics (TODO: remove)
#include <cstring>
#include <limits>
#include <unordered_set>
#include <vector>

namespace {
// vertical_layers / chunk_size truncates; surface layers 16+ need a second chunk row.
int32_t chunk_axis_count(int32_t axis_res, int32_t chunk_size) {
    return std::max(1, (axis_res + chunk_size - 1) / chunk_size);
}

int32_t surface_resolution(double radius, double block_size) {
    const double requested = std::ceil(
        (2.0 * std::max(radius, 1.0)) / std::max(block_size, 0.01));
    uint64_t resolution = 1;
    const uint64_t target = static_cast<uint64_t>(std::min(
        requested, static_cast<double>(std::numeric_limits<int32_t>::max())));
    while (resolution < target && resolution <= (1ull << 30u)) {
        resolution <<= 1u;
    }
    return static_cast<int32_t>(std::min<uint64_t>(
        resolution, static_cast<uint64_t>(std::numeric_limits<int32_t>::max())));
}
}  // namespace

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
    {PlanetFace::PosX, CubeEdge::Left,   PlanetFace::NegZ, CubeEdge::Left,  false, false, false},
    {PlanetFace::PosX, CubeEdge::Right,  PlanetFace::PosZ, CubeEdge::Right, false, false, false},
    {PlanetFace::PosX, CubeEdge::Top,    PlanetFace::PosY, CubeEdge::Right, true,  false, false},
    {PlanetFace::PosX, CubeEdge::Bottom, PlanetFace::NegY, CubeEdge::Right, true,  true,  true },

    {PlanetFace::NegX, CubeEdge::Left,   PlanetFace::PosZ, CubeEdge::Left,  false, false, false},
    {PlanetFace::NegX, CubeEdge::Right,  PlanetFace::NegZ, CubeEdge::Right, false, false, false},
    {PlanetFace::NegX, CubeEdge::Top,    PlanetFace::PosY, CubeEdge::Left,  true,  true,  true },
    {PlanetFace::NegX, CubeEdge::Bottom, PlanetFace::NegY, CubeEdge::Left,  true,  false, false},

    {PlanetFace::PosY, CubeEdge::Top,    PlanetFace::PosZ, CubeEdge::Top,   false, false, false},
    {PlanetFace::PosY, CubeEdge::Bottom, PlanetFace::NegZ, CubeEdge::Top,   false, true,  true },
    {PlanetFace::PosY, CubeEdge::Left,   PlanetFace::NegX, CubeEdge::Top,   true,  true,  true },
    {PlanetFace::PosY, CubeEdge::Right,  PlanetFace::PosX, CubeEdge::Top,   true,  false, false},
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
    surface_sh.horizontal_res =
        surface_resolution(config_.planet.radius, config_.block_size);
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
    config_.surface_shells = std::max(1, config_.surface_shells);
    config_.base_resolution = std::max(1, config_.base_resolution);
    config_.block_size = std::max(0.01, config_.block_size);
    config_.terrain_feature_size = std::max(8.0, config_.terrain_feature_size);
    config_.chunk_size = std::clamp(config_.chunk_size, 1, VoxelChunk::CHUNK_X);
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
    (void)face;
    const ShellConfig &sh = shell_config(shell_idx);
    u = -1.0 + (static_cast<double>(col_x) + 0.5) / static_cast<double>(sh.horizontal_res) * 2.0;
    v = -1.0 + (static_cast<double>(col_z) + 0.5) / static_cast<double>(sh.horizontal_res) * 2.0;
}

// ============================================================================
// Terrain height
// ============================================================================

int32_t BlockWorld::terrain_height_at(const glm::dvec3 &world_dir) const {
    SphereNoise3D noise(config_.seed);
    const float frequency = static_cast<float>(std::max(
        1.0, config_.planet.radius / config_.terrain_feature_size));
    // The integer layer is the floor of the continuous height. The fractional
    // remainder is encoded on the surface block and reused by collision.
    return noise.terrain_height(world_dir, 18.0f, 8.0f, frequency);
}

int32_t BlockWorld::terrain_height_at_face_uv(PlanetFace face, int32_t col_x,
                                              int32_t col_z) const {
    const int32_t res = shell_config(shell_count() - 1).horizontal_res;
    const double u = -1.0 + (static_cast<double>(col_x) + 0.5) / static_cast<double>(res) * 2.0;
    const double v = -1.0 + (static_cast<double>(col_z) + 0.5) / static_cast<double>(res) * 2.0;
    return terrain_height_at(face_uv_to_direction(face, u, v));
}

double BlockWorld::surface_radial_distance(const glm::dvec3 &direction) const {
    const glm::dvec3 dir = glm::normalize(direction);
    // Minecraft-style terrain uses whole blocks. Fractional-height surface
    // cells created hairline trenches and made the ground read as cracked.
    const double height = static_cast<double>(terrain_height_at(dir) + 1);
    const ShellConfig &sh = shell_config(shell_count() - 1);
    const double layer_t = std::clamp(
        height / static_cast<double>(std::max(1, sh.vertical_layers)),
        0.0, 1.0);
    return sh.inner_radius +
           (sh.outer_radius - sh.inner_radius) * layer_t;
}

double BlockWorld::surface_height_above_base(const glm::dvec3 &direction) const {
    return surface_radial_distance(direction) - config_.planet.radius;
}

double BlockWorld::max_surface_height_above_base() const {
    const ShellConfig &sh = shell_config(shell_count() - 1);
    return sh.outer_radius - sh.inner_radius;
}

glm::dvec3 BlockWorld::spawn_position_at_face_uv(PlanetFace face, int32_t col_x,
                                                int32_t col_z,
                                                double radial_clearance) const {
    const int32_t res = shell_config(shell_count() - 1).horizontal_res;
    const double u = -1.0 + (static_cast<double>(col_x) + 0.5) /
                              static_cast<double>(res) * 2.0;
    const double v = -1.0 + (static_cast<double>(col_z) + 0.5) /
                              static_cast<double>(res) * 2.0;
    const glm::dvec3 dir = face_uv_to_direction(face, u, v);
    return config_.planet.center +
           dir * (surface_radial_distance(dir) + radial_clearance);
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

    const int32_t cs = config_.chunk_size;
    std::vector<int32_t> col_heights(static_cast<size_t>(cs * cs), 0);
    auto col_height = [&](int32_t x, int32_t z) -> int32_t & {
        return col_heights[static_cast<size_t>(x * cs + z)];
    };
    for (int32_t z = 0; z < cs; ++z) {
        for (int32_t x = 0; x < cs; ++x) {
            col_height(x, z) = terrain_height_at_face_uv(
                addr.sector, base_col_x + x, base_col_z + z);
        }
    }

    int32_t solid_count = 0;
    for (int32_t z = 0; z < cs; ++z) {
        for (int32_t x = 0; x < cs; ++x) {
            const int32_t surf_h = col_height(x, z);
            for (int32_t y = 0; y < cs; ++y) {
                BlockAddress ba = addr;
                ba.block = glm::ivec3(x, y, z);
                const VoxelMaterial mat = block_material_at(ba, surf_h);
                uint8_t block_h = 0;
                if (mat != VoxelMaterial::Air) {
                    block_h = VoxelChunk::kMaxBlockHeight;
                }
                out.set_material(x, y, z, mat, block_h);
                if (mat != VoxelMaterial::Air) {
                    ++solid_count;
                }
            }
        }
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

size_t BlockWorld::evict_chunks_except(
    const std::vector<BlockAddress> &resident) {
    std::unordered_set<BlockAddress, BlockAddressHash> keep;
    keep.reserve(resident.size());
    for (BlockAddress addr : resident) {
        addr.block = glm::ivec3(0);
        keep.insert(addr);
    }

    const size_t before = chunks_.size();
    std::erase_if(chunks_, [&keep](const auto &entry) {
        return !keep.contains(entry.first);
    });
    return before - chunks_.size();
}

// ============================================================================
// Cross-sector chunk addressing (streaming)
// ============================================================================

bool BlockWorld::offset_chunk_address(const BlockAddress &origin,
                                      int32_t dcx, int32_t dcy, int32_t dcz,
                                      BlockAddress &out) const {
    const ShellConfig &sh = shell_config(origin.shell);
    const int32_t hc = chunk_axis_count(sh.horizontal_res, config_.chunk_size);
    const int32_t vc = chunk_axis_count(sh.vertical_layers, config_.chunk_size);

    int32_t cx = origin.chunk.x;
    int32_t cy = origin.chunk.y + dcy;
    int32_t cz = origin.chunk.z;
    PlanetFace sector = origin.sector;

    if (cy < 0 || cy >= vc) {
        return false;
    }

    // Walk one chunk at a time so multi-step crossings stay consistent with
    // neighbors() edge pairings (Bowerbyte cube-net seam rules).
    auto step_axis = [&](int32_t &coord, int32_t delta,
                         CubeEdge neg_edge, CubeEdge pos_edge) -> bool {
        const int32_t steps = std::abs(delta);
        const int32_t dir = (delta > 0) ? 1 : -1;
        for (int32_t s = 0; s < steps; ++s) {
            const int32_t next = coord + dir;
            if (next >= 0 && next < hc) {
                coord = next;
                continue;
            }

            const CubeEdge edge = (dir > 0) ? pos_edge : neg_edge;
            const auto &p = edge_pairing(sector, edge);
            sector = p.to_face;

            int32_t tcx = next;
            int32_t tcz = cz;
            if (p.swap_uv) {
                std::swap(tcx, tcz);
            }
            if (p.flip_u) {
                tcx = hc - 1 - tcx;
            }
            if (p.flip_v) {
                tcz = hc - 1 - tcz;
            }
            tcx = std::clamp(tcx, 0, hc - 1);
            tcz = std::clamp(tcz, 0, hc - 1);
            coord = tcx;
            cz = tcz;
        }
        return true;
    };

    if (!step_axis(cx, dcx, CubeEdge::Left, CubeEdge::Right)) {
        return false;
    }
    if (!step_axis(cz, dcz, CubeEdge::Bottom, CubeEdge::Top)) {
        return false;
    }

    out = origin;
    out.sector = sector;
    out.shell = origin.shell;
    out.chunk = glm::ivec3(cx, cy, cz);
    out.block = glm::ivec3(0);
    return true;
}

void BlockWorld::collect_stream_chunks(const BlockAddress &player_addr,
                                       int32_t shell, int32_t radius,
                                       std::vector<BlockAddress> &out) const {
    out.clear();
    const ShellConfig &sh = shell_config(shell);
    const int32_t vc = chunk_axis_count(sh.vertical_layers, config_.chunk_size);
    // +1 halo: generate/mesh neighbors so cross-chunk face culling is correct
    // at sector seams and chunk xz boundaries (prevents visible gaps).
    const int32_t stream_r = radius + 1;
    out.reserve(static_cast<size_t>(vc) *
                static_cast<size_t>((2 * stream_r + 1) * (2 * stream_r + 1)));

    // Stream every radial chunk row in the shell, not only the player's row.
    // Surface terrain spans layers 0..N across vc rows; streaming only
    // player.chunk.y left the other row empty → rectangular holes in the mesh.
    for (int32_t cy = 0; cy < vc; ++cy) {
        BlockAddress base = player_addr;
        base.shell = shell;
        base.block = glm::ivec3(0);
        base.chunk.y = cy;

        for (int32_t dz = -stream_r; dz <= stream_r; ++dz) {
            for (int32_t dx = -stream_r; dx <= stream_r; ++dx) {
                BlockAddress addr{};
                if (!offset_chunk_address(base, dx, 0, dz, addr)) {
                    continue;
                }

                bool duplicate = false;
                for (const BlockAddress &existing : out) {
                    if (existing.sector == addr.sector &&
                        existing.shell == addr.shell &&
                        existing.chunk == addr.chunk) {
                        duplicate = true;
                        break;
                    }
                }
                if (!duplicate) {
                    out.push_back(addr);
                }
            }
        }
    }
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
    const int32_t hc = chunk_axis_count(sh.horizontal_res, config_.chunk_size);
    const int32_t vc = chunk_axis_count(sh.vertical_layers, config_.chunk_size);
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
    auto combine = [&h](uint64_t value) {
        h ^= value + 0x9e3779b97f4a7c15ull + (h << 6u) + (h >> 2u);
    };
    // Do not pack fields into overlapping bit ranges: at target planet scale
    // chunk x/z exceed 16 bits and the old layout produced real ID collisions.
    combine(static_cast<uint64_t>(static_cast<uint8_t>(addr.sector)));
    combine(static_cast<uint64_t>(static_cast<uint32_t>(addr.shell)));
    combine(static_cast<uint64_t>(static_cast<uint32_t>(addr.chunk.x)));
    combine(static_cast<uint64_t>(static_cast<uint32_t>(addr.chunk.y)));
    combine(static_cast<uint64_t>(static_cast<uint32_t>(addr.chunk.z)));
    h ^= h >> 30u; h *= 0xbf58476d1ce4e5b9ull;
    h ^= h >> 27u; h *= 0x94d049bb133111ebull;
    h ^= h >> 31u;
    return h == 0 ? 0x424c4f434b504c54ull : h;
}

static uint64_t block_mesh_content_hash(const RenderMesh &mesh) {
    // FNV-1a is sufficient here: this is a cache identity, not a security
    // primitive. Include all geometry attributes so equal-sized remeshes and
    // camera-relative origin shifts cannot reuse stale GPU buffers.
    uint64_t hash = 1469598103934665603ull;
    auto mix = [&hash](const void *data, size_t size) {
        const auto *bytes = static_cast<const uint8_t *>(data);
        for (size_t i = 0; i < size; ++i) {
            hash ^= bytes[i];
            hash *= 1099511628211ull;
        }
    };
    if (!mesh.vertices.empty()) {
        mix(mesh.vertices.data(), mesh.vertices.size() * sizeof(RenderVertex));
    }
    if (!mesh.indices.empty()) {
        mix(mesh.indices.data(), mesh.indices.size() * sizeof(uint32_t));
    }
    if (!mesh.indices16.empty()) {
        mix(mesh.indices16.data(), mesh.indices16.size() * sizeof(uint16_t));
    }
    hash ^= static_cast<uint64_t>(mesh.use_16_bit_indices);
    return hash == 0 ? 1 : hash;
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
    mesh.world_origin = camera_relative_origin;
    mesh.mesh_id = block_chunk_mesh_id(addr);
    (void)solid_at;  // retained for API compat; neighbor queries use find_chunk.
    mesh.vertices.reserve(8192);
    mesh.indices.reserve(12288);

    const ShellConfig &sh = shell_config(addr.shell);
    const double bw = config_.block_size;
    const int32_t cs = config_.chunk_size;
    // LOD stride: at LOD N, only every (2^N)th block is meshed.
    const int32_t stride = 1 << std::clamp(lod_level, 0, 3);

    // WHY per-face meshing instead of greedy meshing:
    // On a sphere, each block has a unique normal pointing away from planet
    // center. Greedy meshing merges adjacent same-material blocks into one
    // quad with a single normal from the quad center, which destroys the
    // spherical curvature and makes terrain look flat. Per-face meshing
    // preserves per-block normals for correct sphere lighting.

    // Sub-voxel height terrain (Ephilem-style):
    // Only the surface block in each column stores a fractional height in
    // [1, kMaxBlockHeight]. Interior blocks are always full height.
    constexpr uint8_t kMaxH = VoxelChunk::kMaxBlockHeight;
    const double kMaxH_d = static_cast<double>(kMaxH);

    auto resolve_neighbor_address = [&](int nx, int ny, int nz,
                                        BlockAddress &nb_addr) -> bool {
        if (nx >= 0 && nx < cs && ny >= 0 && ny < cs && nz >= 0 && nz < cs) {
            nb_addr = addr;
            nb_addr.block = glm::ivec3(nx, ny, nz);
            return true;
        }

        const int hc = chunk_axis_count(sh.horizontal_res, cs);
        const int vc = chunk_axis_count(sh.vertical_layers, cs);

        int cx = addr.chunk.x;
        int cy = addr.chunk.y;
        int cz = addr.chunk.z;

        if (nx < 0) cx -= 1; else if (nx >= cs) cx += 1;
        if (ny < 0) cy -= 1; else if (ny >= cs) cy += 1;
        if (nz < 0) cz -= 1; else if (nz >= cs) cz += 1;

        const int bx_new = (nx % cs + cs) % cs;
        const int by_new = (ny % cs + cs) % cs;
        const int bz_new = (nz % cs + cs) % cs;

        if (cx < 0 || cx >= hc || cz < 0 || cz >= hc || cy < 0 || cy >= vc) {
            if (cy < 0 || cy >= vc) {
                return false;
            }
            CubeEdge crossed_edge;
            if (cx < 0) crossed_edge = CubeEdge::Left;
            else if (cx >= hc) crossed_edge = CubeEdge::Right;
            else if (cz < 0) crossed_edge = CubeEdge::Bottom;
            else crossed_edge = CubeEdge::Top;

            const auto& p = edge_pairing(addr.sector, crossed_edge);
            nb_addr.sector = p.to_face;
            nb_addr.shell = addr.shell;

            int scx = std::clamp(cx, 0, hc - 1);
            int scz = std::clamp(cz, 0, hc - 1);

            if (p.swap_uv) std::swap(scx, scz);
            if (p.flip_u) scx = hc - 1 - scx;
            if (p.flip_v) scz = hc - 1 - scz;

            nb_addr.chunk = glm::ivec3(scx, cy, scz);

            int tbx = bx_new;
            int tbz = bz_new;
            if (p.swap_uv) std::swap(tbx, tbz);
            if (p.flip_u) tbx = cs - 1 - tbx;
            if (p.flip_v) tbz = cs - 1 - tbz;

            nb_addr.block = glm::ivec3(tbx, by_new, tbz);
            return true;
        }

        nb_addr.sector = addr.sector;
        nb_addr.shell = addr.shell;
        nb_addr.chunk = glm::ivec3(cx, cy, cz);
        nb_addr.block = glm::ivec3(bx_new, by_new, bz_new);
        return true;
    };

    auto get_neighbor_height = [&](int nx, int ny, int nz,
                                   BlockAddress &nb_addr,
                                   bool &neighbor_exists) -> uint8_t {
        if (!resolve_neighbor_address(nx, ny, nz, nb_addr)) {
            neighbor_exists = false;
            return 0;
        }

        BlockAddress chunk_key = nb_addr;
        chunk_key.block = glm::ivec3(0);
        const VoxelChunk *nc = (chunk_key.sector == addr.sector &&
                                chunk_key.shell == addr.shell &&
                                chunk_key.chunk == addr.chunk)
            ? &chunk
            : find_chunk(chunk_key);
        if (nc == nullptr) {
            neighbor_exists = false;
            return 0;
        }

        neighbor_exists = true;
        if (!nc->solid(nb_addr.block.x, nb_addr.block.y, nb_addr.block.z)) {
            return 0;
        }
        return nc->block_height(nb_addr.block.x, nb_addr.block.y, nb_addr.block.z);
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

    auto texture_layer = [](VoxelMaterial material, bool top_face) -> float {
        if (material == VoxelMaterial::Grass && top_face) return 0.0f;
        if (material == VoxelMaterial::Grass) return 3.0f;
        if (material == VoxelMaterial::Stone) return 2.0f;
        return 1.0f;
    };

    auto emit_quad = [&](glm::dvec3 v0, glm::dvec3 v1, glm::dvec3 v2, glm::dvec3 v3,
                         const glm::vec3 &color, const glm::dvec3 &outward_hint,
                         float material_layer) {
        glm::dvec3 fn = glm::cross(v1 - v0, v2 - v0);
        if (glm::dot(fn, fn) < 1e-18) {
            return;
        }
        fn = glm::normalize(fn);

        // Reconcile winding against an outward reference so back-face culling
        // stays correct on curved cube-sphere blocks.
        if (glm::dot(fn, outward_hint) < 0.0) {
            std::swap(v1, v3);
            fn = -fn;
        }

        const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
        const glm::dvec3 rel = camera_relative_origin;
        const float repeat_u = static_cast<float>(
            std::max(glm::length(v1 - v0) / std::max(bw, 0.01), 0.001));
        const float repeat_v = static_cast<float>(
            std::max(glm::length(v3 - v0) / std::max(bw, 0.01), 0.001));
        mesh.vertices.push_back(RenderVertex{
            glm::vec3(v0 - rel), color, glm::vec3(fn),
            glm::vec3(0.0f, 0.0f, material_layer)});
        mesh.vertices.push_back(RenderVertex{
            glm::vec3(v1 - rel), color, glm::vec3(fn),
            glm::vec3(repeat_u, 0.0f, material_layer)});
        mesh.vertices.push_back(RenderVertex{
            glm::vec3(v2 - rel), color, glm::vec3(fn),
            glm::vec3(repeat_u, repeat_v, material_layer)});
        mesh.vertices.push_back(RenderVertex{
            glm::vec3(v3 - rel), color, glm::vec3(fn),
            glm::vec3(0.0f, repeat_v, material_layer)});
        mesh.indices.insert(mesh.indices.end(), {
            base, base + 1, base + 2,
            base, base + 2, base + 3});
    };

    const double res = static_cast<double>(sh.horizontal_res);
    const double vlayers = static_cast<double>(std::max(1, sh.vertical_layers));
    const int32_t grid_w = (cs + stride - 1) / stride;

    auto up_face_visible = [&](int32_t bx, int32_t by, int32_t bz) -> bool {
        if (!chunk.solid(bx, by, bz)) {
            return false;
        }
        if (chunk.block_height(bx, by, bz) == 0) {
            return false;
        }
        const glm::ivec3 dv = block_dir_vector(BlockDir::Up);
        BlockAddress nb_addr = addr;
        bool neighbor_exists = false;
        const uint8_t nbh = get_neighbor_height(
            bx + dv.x * stride, by + dv.y * stride, bz + dv.z * stride,
            nb_addr, neighbor_exists);
        return !(neighbor_exists && nbh > 0);
    };

    // Pass 1: greedy-merge exposed Up faces per horizontal slice (0fps-style).
    // WHY: surface columns are mostly grass tops; merging cuts verts ~4-16x
    // while each merged quad keeps true spherical corner positions.
    std::vector<int16_t> up_mask(static_cast<size_t>(grid_w * grid_w), 0);
    for (int32_t by = 0; by < cs; by += stride) {
        std::fill(up_mask.begin(), up_mask.end(), 0);
        for (int32_t iz = 0; iz < grid_w; ++iz) {
            for (int32_t ix = 0; ix < grid_w; ++ix) {
                const int32_t bx = ix * stride;
                const int32_t bz = iz * stride;
                if (!up_face_visible(bx, by, bz)) {
                    continue;
                }
                const VoxelMaterial mat = chunk.material(bx, by, bz);
                if (mat == VoxelMaterial::Air) {
                    continue;
                }
                // Top faces may only merge when their encoded heights match.
                // Merging by material alone reuses the first cell's top radius
                // and turns fractional terrain into large flat plates.
                up_mask[static_cast<size_t>(iz * grid_w + ix)] =
                    static_cast<int16_t>(static_cast<uint8_t>(mat) *
                                         (kMaxH + 1) +
                                         chunk.block_height(bx, by, bz));
            }
        }

        for (int32_t iz = 0; iz < grid_w; ++iz) {
            for (int32_t ix = 0; ix < grid_w;) {
                const int16_t face_key =
                    up_mask[static_cast<size_t>(iz * grid_w + ix)];
                if (face_key == 0) {
                    ++ix;
                    continue;
                }

                int32_t width = 1;
                while (ix + width < grid_w &&
                       up_mask[static_cast<size_t>(iz * grid_w + ix + width)] ==
                           face_key) {
                    ++width;
                }

                int32_t height = 1;
                bool done = false;
                while (iz + height < grid_w && !done) {
                    for (int32_t k = 0; k < width; ++k) {
                        if (up_mask[static_cast<size_t>((iz + height) * grid_w +
                                                        ix + k)] != face_key) {
                            done = true;
                            break;
                        }
                    }
                    if (!done) {
                        ++height;
                    }
                }

                const int32_t bx = ix * stride;
                const int32_t bz = iz * stride;
                const VoxelMaterial face_mat = static_cast<VoxelMaterial>(
                    face_key / (kMaxH + 1));
                const uint8_t bh = chunk.block_height(bx, by, bz);

                const double gx0 = static_cast<double>(addr.chunk.x * cs + bx);
                const double gx1 =
                    static_cast<double>(addr.chunk.x * cs + bx + width * stride);
                const double gy0 = static_cast<double>(addr.chunk.y * cs + by);
                const double gy1 = gy0 + static_cast<double>(stride);
                const double gz0 = static_cast<double>(addr.chunk.z * cs + bz);
                const double gz1 =
                    static_cast<double>(addr.chunk.z * cs + bz + height * stride);

                const double u0 = -1.0 + gx0 / res * 2.0;
                const double u1 = -1.0 + gx1 / res * 2.0;
                const double v0 = -1.0 + gz0 / res * 2.0;
                const double v1 = -1.0 + gz1 / res * 2.0;

                const double r_cell0 =
                    sh.inner_radius +
                    (sh.outer_radius - sh.inner_radius) * (gy0 / vlayers);
                const double r_cell1 =
                    sh.inner_radius +
                    (sh.outer_radius - sh.inner_radius) * (gy1 / vlayers);
                const double r_top =
                    r_cell0 + (r_cell1 - r_cell0) *
                                  (static_cast<double>(bh) / kMaxH_d);

                const glm::dvec3 d00 = face_uv_to_direction(addr.sector, u0, v0);
                const glm::dvec3 d10 = face_uv_to_direction(addr.sector, u1, v0);
                const glm::dvec3 d01 = face_uv_to_direction(addr.sector, u0, v1);
                const glm::dvec3 d11 = face_uv_to_direction(addr.sector, u1, v1);

                const glm::dvec3 v0w = config_.planet.center + d00 * r_top;
                const glm::dvec3 v1w = config_.planet.center + d10 * r_top;
                const glm::dvec3 v2w = config_.planet.center + d11 * r_top;
                const glm::dvec3 v3w = config_.planet.center + d01 * r_top;

                const float height_t = std::clamp(
                    static_cast<float>(gy0) /
                        static_cast<float>(std::max(1, sh.vertical_layers)),
                    0.0f, 1.0f);
                const glm::vec3 color = VoxelChunk::material_color(
                    face_mat, true, height_t);
                const glm::dvec3 face_center = (v0w + v1w + v2w + v3w) * 0.25;
                emit_quad(v0w, v1w, v2w, v3w, color,
                          face_center - config_.planet.center,
                          texture_layer(face_mat, true));

                for (int32_t row = 0; row < height; ++row) {
                    for (int32_t col = 0; col < width; ++col) {
                        up_mask[static_cast<size_t>((iz + row) * grid_w + ix + col)] =
                            0;
                    }
                }
                ix += width;
            }
        }
    }

    // Pass 2: side/down faces per column, top-down. Fully buried blocks below
    // the exposed surface shell are skipped (Craft-style empty-space culling).
    for (int32_t bz = 0; bz < cs; bz += stride) {
        for (int32_t bx = 0; bx < cs; bx += stride) {
            int32_t top_by = -1;
            for (int32_t scan_y = cs - 1; scan_y >= 0; scan_y -= stride) {
                if (!chunk.solid(bx, scan_y, bz)) {
                    continue;
                }
                if (chunk.material(bx, scan_y, bz) == VoxelMaterial::Air) {
                    continue;
                }
                top_by = scan_y;
                break;
            }
            if (top_by < 0) {
                continue;
            }

            for (int32_t by = top_by; by >= 0; by -= stride) {
                const size_t verts_before = mesh.vertices.size();
                if (!chunk.solid(bx, by, bz)) {
                    break;
                }
                const VoxelMaterial mat = chunk.material(bx, by, bz);
                if (mat == VoxelMaterial::Air) {
                    break;
                }

                const uint8_t bh = chunk.block_height(bx, by, bz);

                // Evaluate 8 corners of the frustum block.
                const double gx0 = static_cast<double>(addr.chunk.x * cs + bx);
                const double gx1 = gx0 + stride;
                const double gy0 = static_cast<double>(addr.chunk.y * cs + by);
                const double gy1 = gy0 + stride;
                const double gz0 = static_cast<double>(addr.chunk.z * cs + bz);
                const double gz1 = gz0 + stride;

                const double u0 = -1.0 + (gx0) / res * 2.0;
                const double u1 = -1.0 + (gx1) / res * 2.0;
                const double v0 = -1.0 + (gz0) / res * 2.0;
                const double v1 = -1.0 + (gz1) / res * 2.0;

                // Full-cell radial extents (0% and 100% height).
                const double r_cell0 = sh.inner_radius + (sh.outer_radius - sh.inner_radius) * (gy0 / vlayers);
                const double r_cell1 = sh.inner_radius + (sh.outer_radius - sh.inner_radius) * (gy1 / vlayers);

                // Actual block top/bottom based on sub-voxel height.
                // Block top radius interpolates from cell bottom to cell top based on height fraction.
                const double r_block_top = r_cell0 + (r_cell1 - r_cell0) * (static_cast<double>(bh) / kMaxH_d);

                const glm::dvec3 d00 = face_uv_to_direction(addr.sector, u0, v0);
                const glm::dvec3 d10 = face_uv_to_direction(addr.sector, u1, v0);
                const glm::dvec3 d01 = face_uv_to_direction(addr.sector, u0, v1);
                const glm::dvec3 d11 = face_uv_to_direction(addr.sector, u1, v1);

                // p_full[0..3] = bottom face (r_cell0), p_full[4..7] = top full (r_cell1).
                const glm::dvec3 p_full[8] = {
                    config_.planet.center + d00 * r_cell0,
                    config_.planet.center + d10 * r_cell0,
                    config_.planet.center + d01 * r_cell0,
                    config_.planet.center + d11 * r_cell0,
                    config_.planet.center + d00 * r_cell1,
                    config_.planet.center + d10 * r_cell1,
                    config_.planet.center + d01 * r_cell1,
                    config_.planet.center + d11 * r_cell1,
                };

                // Height fraction for color tinting.
                const float height_t = std::clamp(
                    static_cast<float>(gy0) / static_cast<float>(std::max(1, sh.vertical_layers)),
                    0.0f, 1.0f);

                const glm::dvec3 cell_mid =
                    (p_full[0] + p_full[1] + p_full[2] + p_full[3] +
                     p_full[4] + p_full[5] + p_full[6] + p_full[7]) *
                    0.125;

                // Classify each face direction for sub-voxel height handling.
                // Side faces = Left, Right, Back, Front (horizontal directions)
                // Top/Bottom = Up, Down (radial directions)
                auto is_side_face = [](BlockDir fd) -> bool {
                    return fd == BlockDir::Left || fd == BlockDir::Right ||
                           fd == BlockDir::Back || fd == BlockDir::Front;
                };

                for (const auto &face : faces) {
                    const bool side = is_side_face(face.fd);

                    if (side) {
                        const glm::ivec3 dv = block_dir_vector(face.fd);
                        const int nx = bx + dv.x * stride;
                        const int ny = by + dv.y * stride;
                        const int nz = bz + dv.z * stride;

                        BlockAddress nb_addr = addr;
                        bool neighbor_exists = false;
                        const uint8_t nbh = get_neighbor_height(nx, ny, nz, nb_addr, neighbor_exists);
                        const bool neighbor_solid = neighbor_exists && nbh > 0;

                        if (bh == 0) continue;

                        // Side walls terminate at the same fractional top as the
                        // Up face so geometry and the collision heightfield agree.
                        const double r_my_top = r_block_top;
                        double r_emit_bottom = r_cell0;
                        if (neighbor_solid) {
                            const double r_nb_top =
                                (nbh >= kMaxH)
                                    ? r_cell1
                                    : r_cell0 + (r_cell1 - r_cell0) *
                                                      (static_cast<double>(nbh) /
                                                       kMaxH_d);
                            if (r_my_top <= r_nb_top + 1e-9) {
                                continue;
                            }
                            r_emit_bottom = std::max(r_cell0, r_nb_top);
                        }
                        if (r_my_top <= r_emit_bottom + 1e-9) {
                            continue;
                        }

                        glm::dvec3 v0, v1, v2, v3;
                        if (face.fd == BlockDir::Left || face.fd == BlockDir::Right) {
                            const glm::dvec3 side_dir0 =
                                face.fd == BlockDir::Left ? d00 : d10;
                            const glm::dvec3 side_dir1 =
                                face.fd == BlockDir::Left ? d01 : d11;
                            const glm::dvec3 p_side_z0[2] = {
                                config_.planet.center + side_dir0 * r_emit_bottom,
                                config_.planet.center + side_dir0 * r_my_top,
                            };
                            const glm::dvec3 p_side_z1[2] = {
                                config_.planet.center + side_dir1 * r_emit_bottom,
                                config_.planet.center + side_dir1 * r_my_top,
                            };
                            v0 = p_side_z0[0]; v1 = p_side_z1[0];
                            v2 = p_side_z1[1]; v3 = p_side_z0[1];
                        } else {
                            const glm::dvec3 side_dir0 =
                                face.fd == BlockDir::Back ? d00 : d01;
                            const glm::dvec3 side_dir1 =
                                face.fd == BlockDir::Back ? d10 : d11;
                            const glm::dvec3 p_side_x0[2] = {
                                config_.planet.center + side_dir0 * r_emit_bottom,
                                config_.planet.center + side_dir0 * r_my_top,
                            };
                            const glm::dvec3 p_side_x1[2] = {
                                config_.planet.center + side_dir1 * r_emit_bottom,
                                config_.planet.center + side_dir1 * r_my_top,
                            };
                            v0 = p_side_x0[0]; v1 = p_side_x0[1];
                            v2 = p_side_x1[1]; v3 = p_side_x1[0];
                        }

                        const bool top_face = false;
                        const glm::vec3 color =
                            VoxelChunk::material_color(mat, top_face, height_t);
                        const glm::dvec3 face_center = (v0 + v1 + v2 + v3) * 0.25;
                        emit_quad(v0, v1, v2, v3, color, face_center - cell_mid,
                                  texture_layer(mat, false));
                    } else if (face.fd != BlockDir::Up) {
                        // ── Down face (bottom): original logic ──
                        // Bottom face of the block: emitted when neighbor below is
                        // not solid. The quad spans the full cell bottom.
                        const glm::ivec3 dv = block_dir_vector(face.fd);
                        const int nx = bx + dv.x * stride;
                        const int ny = by + dv.y * stride;
                        const int nz = bz + dv.z * stride;

                        BlockAddress nb_addr = addr;
                        bool neighbor_exists = false;
                        const uint8_t nbh = get_neighbor_height(nx, ny, nz, nb_addr, neighbor_exists);

                        // Down face visible when neighbor below has less than full height
                        // (creating a gap at the cell bottom) or neighbor is absent.
                        if (neighbor_exists && nbh >= kMaxH) continue;

                        const glm::dvec3 v0 = p_full[face.corners[0]];
                        const glm::dvec3 v1 = p_full[face.corners[1]];
                        const glm::dvec3 v2 = p_full[face.corners[2]];
                        const glm::dvec3 v3 = p_full[face.corners[3]];

                        const bool top_face = false;
                        const glm::vec3 color =
                            VoxelChunk::material_color(mat, top_face, height_t);
                        const glm::dvec3 face_center = (v0 + v1 + v2 + v3) * 0.25;
                        emit_quad(v0, v1, v2, v3, color,
                                  config_.planet.center - face_center,
                                  texture_layer(mat, false));
                    }
                }

                if (mesh.vertices.size() == verts_before) {
                    break;
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
    mesh.content_hash = block_mesh_content_hash(mesh);
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
                         float lacunarity, float gain,
                         float base_frequency) const {
    float value = 0.0f, amplitude = 1.0f;
    float freq = std::max(base_frequency, 0.001f);
    float maxv = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        value += sample(direction, freq) * amplitude;
        maxv += amplitude;
        freq *= lacunarity;
        amplitude *= gain;
    }
    return value / maxv;
}

float SphereNoise3D::terrain_height_raw(const glm::dvec3 &direction,
                                        float base_height,
                                        float amplitude,
                                        float base_frequency) const {
    // Sample the unit direction directly. Face-local UV noise changes basis at
    // cube seams; 3D noise on the sphere remains continuous across all faces.
    const glm::dvec3 dir = glm::normalize(direction);
    const float macro = fbm(dir, 5, 2.0f, 0.5f, base_frequency);
    const float detail = fbm(dir, 3, 2.1f, 0.5f, base_frequency * 8.0f);
    return base_height + macro * amplitude + detail * amplitude * 0.2f;
}

int32_t SphereNoise3D::terrain_height(const glm::dvec3 &direction,
                                       float base_height,
                                       float amplitude,
                                       float base_frequency) const {
    const float h = terrain_height_raw(direction, base_height, amplitude,
                                       base_frequency);
    return std::clamp(static_cast<int32_t>(std::floor(h)), 10, 26);
}

float SphereNoise3D::terrain_surface_fraction(const glm::dvec3 &direction,
    float base_height,
    float amplitude,
    float base_frequency) const {
    const float h = terrain_height_raw(direction, base_height, amplitude,
                                       base_frequency);
    return std::clamp(h - std::floor(h), 0.0f, 0.999999f);
}

RenderMesh build_planet_flight_clipmap(
    const BlockWorld &world,
    const glm::dvec3 &center_direction,
    double camera_altitude,
    const glm::dvec3 &camera_relative_origin,
    int32_t cells_per_ring,
    int32_t ring_count) {
    RenderMesh mesh{};
    mesh.world_origin = camera_relative_origin;
    if (!world.initialized()) {
        return mesh;
    }

    const int32_t cells = std::clamp(cells_per_ring, 16, 64);
    const int32_t rings = std::clamp(ring_count, 1, 5);
    const PlanetDefinition &planet = world.planet();
    const glm::dvec3 up = glm::normalize(center_direction);
    const PlanetTangentBasis basis = tangent_basis(up);
    const double radius = planet.radius;
    const double base_half_extent = std::max(
        768.0, std::min(radius * 0.08, camera_altitude * 1.5 + 512.0));
    const double max_half_extent = radius * 0.65;
    const SphereNoise3D geology_noise(world.config().seed ^ 0x9e3779b97f4a7c15ull);
    const float geology_frequency = static_cast<float>(std::max(
        8.0, radius / 8'192.0));
    // A one-metre heightfield becomes sub-pixel within a few hundred metres.
    // Coarser rings therefore preserve the min/max relief of the fine voxels
    // with a gradual vertical scale, analogous to conservative voxel mipmaps.
    const double vertical_lod_scale = std::clamp(
        1.0 + std::sqrt(std::max(0.0, camera_altitude - 256.0) / 256.0) * 3.0,
        1.0, 64.0);

    mesh.vertices.reserve(static_cast<size_t>(rings * cells * cells * 8));
    mesh.indices.reserve(static_cast<size_t>(rings * cells * cells * 12));

    auto direction_at = [&](double east_m, double north_m) {
        return glm::normalize(up * radius + basis.east * east_m +
                              basis.north * north_m);
    };

    auto append_quad = [&](glm::dvec3 p0, glm::dvec3 p1, glm::dvec3 p2,
                           glm::dvec3 p3, const glm::vec3 &color,
                           float texture_layer, float repeat_u,
                           float repeat_v) {
        glm::dvec3 normal = glm::cross(p1 - p0, p2 - p0);
        if (glm::dot(normal, normal) < 1.0e-12) {
            return;
        }
        normal = glm::normalize(normal);
        const glm::dvec3 outward = (p0 + p1 + p2 + p3) * 0.25 - planet.center;
        if (glm::dot(normal, outward) < 0.0) {
            std::swap(p1, p3);
            normal = -normal;
        }

        const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
        const glm::vec3 n(normal);
        mesh.vertices.push_back({glm::vec3(p0 - camera_relative_origin), color, n,
                                 glm::vec3(0.0f, 0.0f, texture_layer)});
        mesh.vertices.push_back({glm::vec3(p1 - camera_relative_origin), color, n,
                                 glm::vec3(repeat_u, 0.0f, texture_layer)});
        mesh.vertices.push_back({glm::vec3(p2 - camera_relative_origin), color, n,
                                 glm::vec3(repeat_u, repeat_v, texture_layer)});
        mesh.vertices.push_back({glm::vec3(p3 - camera_relative_origin), color, n,
                                 glm::vec3(0.0f, repeat_v, texture_layer)});
        mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2,
                                                 base, base + 2, base + 3});
    };

    double previous_half_extent = 0.0;
    for (int32_t ring = 0; ring < rings; ++ring) {
        const double half_extent = std::min(
            max_half_extent, base_half_extent * std::pow(3.0, ring));
        if (ring > 0 && half_extent <= previous_half_extent + 1.0) {
            break;
        }
        const double inner_extent = ring == 0 ? 0.0 : previous_half_extent * 0.90;
        const double cell_size = (half_extent * 2.0) / static_cast<double>(cells);
        // Adjacent clipmap levels overlap by ten percent. Sink each coarser
        // level slightly so the overlap behaves like a depth-ordered
        // transition band instead of two coplanar surfaces fighting and
        // flashing sky-coloured seams during motion.
        const double ring_depth_bias = static_cast<double>(ring) * 1.5;
        const int32_t sample_dim = cells + 2;
        std::vector<double> sample_heights(
            static_cast<size_t>(sample_dim * sample_dim), 0.0);

        auto sample_index = [sample_dim](int32_t x, int32_t z) {
            return static_cast<size_t>(z * sample_dim + x);
        };
        for (int32_t z = 0; z < sample_dim; ++z) {
            const double north_m = -half_extent +
                (static_cast<double>(z) - 0.5) * cell_size;
            for (int32_t x = 0; x < sample_dim; ++x) {
                const double east_m = -half_extent +
                    (static_cast<double>(x) - 0.5) * cell_size;
                const double fine_height = world.surface_height_above_base(
                    direction_at(east_m, north_m));
                sample_heights[sample_index(x, z)] = std::max(
                    0.0, 18.0 + (fine_height - 18.0) * vertical_lod_scale);
            }
        }

        for (int32_t z = 0; z < cells; ++z) {
            const double z0 = -half_extent + static_cast<double>(z) * cell_size;
            const double z1 = z0 + cell_size;
            const double cz = (z0 + z1) * 0.5;
            for (int32_t x = 0; x < cells; ++x) {
                const double x0 = -half_extent + static_cast<double>(x) * cell_size;
                const double x1 = x0 + cell_size;
                const double cx = (x0 + x1) * 0.5;
                if (ring > 0 && std::abs(cx) < inner_extent &&
                    std::abs(cz) < inner_extent) {
                    continue;
                }

                const double height = sample_heights[sample_index(x + 1, z + 1)];
                const glm::dvec3 center_dir = direction_at(cx, cz);
                const int32_t terrain_layer = world.terrain_height_at(center_dir);
                const double local_relief = std::max({
                    std::abs(height - sample_heights[sample_index(x, z + 1)]),
                    std::abs(height - sample_heights[sample_index(x + 2, z + 1)]),
                    std::abs(height - sample_heights[sample_index(x + 1, z)]),
                    std::abs(height - sample_heights[sample_index(x + 1, z + 2)])});
                const float geology = geology_noise.sample(
                    center_dir, geology_frequency);
                const bool rocky = terrain_layer >= 23 || local_relief > 12.0 ||
                                   geology > 0.48f;
                const bool bare_soil = !rocky && geology < -0.50f;
                const VoxelMaterial top_material = rocky
                    ? VoxelMaterial::Stone
                    : (bare_soil ? VoxelMaterial::Dirt : VoxelMaterial::Grass);
                const float height_t = std::clamp(
                    static_cast<float>(height / std::max(
                        1.0, world.max_surface_height_above_base())), 0.0f, 1.0f);
                const glm::vec3 top_color = VoxelChunk::material_color(
                    top_material, true, height_t);
                const float top_texture = rocky ? 2.0f : (bare_soil ? 1.0f : 0.0f);
                // Sink the clipmap slightly beneath editable one-metre blocks,
                // preventing z-fighting where the two representations overlap.
                const double top_radius = std::max(
                    radius + 0.5,
                    radius + height - 0.35 - ring_depth_bias);
                const glm::dvec3 d00 = direction_at(x0, z0);
                const glm::dvec3 d10 = direction_at(x1, z0);
                const glm::dvec3 d11 = direction_at(x1, z1);
                const glm::dvec3 d01 = direction_at(x0, z1);
                append_quad(planet.center + d00 * top_radius,
                            planet.center + d10 * top_radius,
                            planet.center + d11 * top_radius,
                            planet.center + d01 * top_radius,
                            top_color, top_texture, 1.0f, 1.0f);

                const double neighbor_heights[4] = {
                    sample_heights[sample_index(x, z + 1)],
                    sample_heights[sample_index(x + 2, z + 1)],
                    sample_heights[sample_index(x + 1, z)],
                    sample_heights[sample_index(x + 1, z + 2)],
                };
                const glm::dvec3 edge_a[4] = {d00, d10, d00, d01};
                const glm::dvec3 edge_b[4] = {d01, d11, d10, d11};
                for (int32_t edge = 0; edge < 4; ++edge) {
                    const double lower_height = neighbor_heights[edge];
                    if (height <= lower_height + 0.125) {
                        continue;
                    }
                    const double bottom_radius = std::max(
                        radius + 0.25,
                        radius + lower_height - 0.35 - ring_depth_bias);
                    const float side_repeat_v = static_cast<float>(std::clamp(
                        height - lower_height, 1.0, 16.0));
                    const float side_texture = rocky ? 2.0f : 3.0f;
                    const glm::vec3 side_color = VoxelChunk::material_color(
                        rocky ? VoxelMaterial::Stone : VoxelMaterial::Dirt,
                        false, height_t);
                    append_quad(planet.center + edge_a[edge] * bottom_radius,
                                planet.center + edge_b[edge] * bottom_radius,
                                planet.center + edge_b[edge] * top_radius,
                                planet.center + edge_a[edge] * top_radius,
                                side_color, side_texture, 1.0f, side_repeat_v);
                }
            }
        }
        previous_half_extent = half_extent;
    }

    mesh.material = static_cast<uint8_t>(VoxelMaterial::Grass);
    mesh.content_hash = block_mesh_content_hash(mesh);
    return mesh;
}
