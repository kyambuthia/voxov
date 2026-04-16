#pragma once

#include "engine_net_proto/net_protocol_helpers.hpp"
#include "engine_world/net_chunk_state.hpp"
#include "engine_world/world_gen.hpp"

inline NetChunkState net_make_flat_chunk_state(NetChunkCoord coord) {
  return net_make_flat_chunk_state(coord, 1, k_voxov_flat_world_seed);
}

inline NetChunkState net_make_flat_chunk_state(NetChunkCoord coord,
                                               uint32_t version) {
  return net_make_flat_chunk_state(coord, version, k_voxov_flat_world_seed);
}

inline NetChunkState net_make_spherical_chunk_state(NetChunkCoord coord) {
  return net_make_spherical_chunk_state(coord, 1, k_voxov_flat_world_seed);
}

inline NetChunkState net_make_spherical_chunk_state(NetChunkCoord coord,
                                                    uint32_t version) {
  return net_make_spherical_chunk_state(coord, version,
                                        k_voxov_flat_world_seed);
}
