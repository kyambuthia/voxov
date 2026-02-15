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
    bool solid(int x, int y, int z) const;

    RenderMesh build_naive_mesh() const;
    RenderMesh build_debug_grid(float span, float step) const;
    RenderMesh build_sky_placeholder(float size) const;

private:
    size_t index(int x, int y, int z) const;
    std::array<uint8_t, CHUNK_X * CHUNK_Y * CHUNK_Z> voxels{};
};
