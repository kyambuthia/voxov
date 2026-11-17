#pragma once

#include "engine_world/voxel_chunk.hpp"

#include <cstdint>

// 6-face bitmask for Tommo/Luanti-style BFS portal cave occlusion culling.
// Bit i (0..5) corresponds to {-X, +X, -Y, +Y, -Z, +Z}.
struct ChunkFaceVisibility {
  // connectivity[in_face] is a 6-bit mask of faces reachable from in_face
  // through connected air voxels inside the chunk.
  uint8_t reach[6]{};

  bool can_traverse(int in_face, int out_face) const {
    if (in_face < 0 || in_face >= 6 || out_face < 0 || out_face >= 6) {
      return false;
    }
    return (reach[in_face] & static_cast<uint8_t>(1u << out_face)) != 0;
  }
};

ChunkFaceVisibility compute_chunk_face_visibility(const VoxelChunk &chunk);
