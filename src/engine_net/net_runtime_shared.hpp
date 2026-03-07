#pragma once

#include "engine_net/net_common.hpp"
#include "engine_world/voxel_chunk.hpp"
#include "engine_world/world_gen.hpp"

#include <cstddef>
#include <string>

inline std::string net_fixed_string(const char *data, size_t size) {
  if (!data || size == 0) {
    return std::string();
  }
  size_t len = 0;
  while (len < size && data[len] != '\0') {
    ++len;
  }
  return std::string(data, len);
}

inline std::string net_session_status_line(const NetSessionInfo &info) {
  std::string name =
      net_fixed_string(info.server_name, sizeof(info.server_name));
  if (name.empty()) {
    name = "VOXOV Session";
  }
  const std::string mode =
      net_session_flag_set(info.flags, NetSessionFlags::LoopbackOnly) ? "LOCAL"
                                                                      : "WI-FI";
  return name + " [" + std::to_string(info.current_players) + "/" +
         std::to_string(info.max_players) + "] " + mode;
}

inline NetChunkState net_make_flat_chunk_state(
    NetChunkCoord coord, uint32_t version = 1,
    uint64_t world_seed = k_voxov_flat_world_seed) {
  NetChunkState state{};
  state.coord = coord;
  state.version = version;
  state.world_seed = world_seed;
  state.content_type =
      static_cast<uint8_t>(NetChunkContentType::ProceduralFlat);
  return state;
}

inline NetChunkState net_make_spherical_chunk_state(
    NetChunkCoord coord, uint32_t version = 1,
    uint64_t world_seed = k_voxov_flat_world_seed) {
  NetChunkState state{};
  state.coord = coord;
  state.version = version;
  state.world_seed = world_seed;
  state.content_type = static_cast<uint8_t>(NetChunkContentType::SphericalPlanet);
  return state;
}

inline bool net_chunk_state_matches(const NetChunkState &lhs,
                                    const NetChunkState &rhs) {
  return lhs.coord.x == rhs.coord.x && lhs.coord.z == rhs.coord.z &&
         lhs.version == rhs.version && lhs.world_seed == rhs.world_seed &&
         lhs.content_type == rhs.content_type;
}

inline void net_generate_chunk_from_state(VoxelChunk &chunk,
                                          const NetChunkState &state) {
  switch (static_cast<NetChunkContentType>(state.content_type)) {
  case NetChunkContentType::SphericalPlanet:
    chunk.generate_spherical_planet_seeded(state.world_seed);
    break;
  case NetChunkContentType::ProceduralFlat:
  default:
    generate_flat_world_locomotion_chunk(chunk, state.world_seed, state.coord.x,
                                         state.coord.z);
    break;
  }
}
