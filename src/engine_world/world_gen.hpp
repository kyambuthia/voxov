#pragma once

#include <cstdint>

struct VoxelChunkCoord2D {
    int32_t x = 0;
    int32_t z = 0;
};

class WorldGenerator {
public:
    explicit WorldGenerator(uint64_t world_seed = 0);

    uint64_t world_seed() const;
    uint64_t chunk_seed(VoxelChunkCoord2D coord) const;
    float sample_height(float world_x, float world_z) const;

private:
    uint64_t seed = 0;
};
