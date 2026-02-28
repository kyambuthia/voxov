#pragma once

#include <array>
#include <cstdint>

#include "engine_render/render_types.hpp"

class VoxelChunk {
public:
    static constexpr int CHUNK_X = 16;
    static constexpr int CHUNK_Y = 16;
    static constexpr int CHUNK_Z = 16;

    void generate_heightmap_terrain();
    void generate_heightmap_terrain_seeded(uint64_t world_seed, int32_t chunk_x, int32_t chunk_z);
    void generate_flat_ground(int ground_y);
    bool solid(int x, int y, int z) const;
    void set_solid(int x, int y, int z, bool value);

    RenderMesh build_naive_mesh() const;
    RenderMesh build_debug_grid(float span, float step) const;
    RenderMesh build_sky_placeholder(float size) const;

private:
    size_t index(int x, int y, int z) const;
    std::array<uint8_t, CHUNK_X * CHUNK_Y * CHUNK_Z> voxels{};
};
