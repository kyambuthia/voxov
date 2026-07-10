#pragma once

#include "engine_net_proto/net_types.hpp"
#include "engine_world/voxel_chunk.hpp"

inline void net_generate_chunk_from_state(VoxelChunk &chunk,
                                          const NetChunkState &state) {
  switch (static_cast<NetChunkContentType>(state.content_type)) {
  case NetChunkContentType::SphericalPlanet:
  default:
    chunk.generate_spherical_planet_seeded(state.world_seed);
    break;
  }
}
