#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

// A ship hull loaded from a MagicaVoxel .vox file, reduced at load time to the
// quads that actually face air. Ships are authored by
// tools/generate_ship_voxels.py; the caller picks the voxel edge length
// (see kShipVoxelSizeMeters).
//
// Model space matches SpaceshipState: +Z nose, +Y roof, +X right, origin at the
// centre of the hull's bounding box so the craft rotates about itself.
struct VoxelQuad {
  glm::vec3 corners[4]{};   // counter-clockwise seen from outside
  glm::vec3 normal{0.0f};
  glm::vec3 color{1.0f};
  uint8_t material = 0;   // .vox palette index, see the ship .json sidecar
};

// Palette index of the engine nozzle, which the renderer tints with thrust.
inline constexpr uint8_t kVoxelThrusterMaterial = 4;

struct VoxelModel {
  std::vector<VoxelQuad> quads;
  glm::vec3 size_meters{0.0f};
  bool valid = false;
};

// Parses `data` as a .vox file. Returns an invalid model on malformed input
// rather than throwing; the caller falls back to primitive geometry.
VoxelModel parse_vox_model(const std::vector<uint8_t> &data,
                           float voxel_size_meters = 1.0f);

// Uses PlatformServices asset roots, including the working directory and
// executable directory. Parsed results are cached by path and voxel scale.
const VoxelModel &load_voxel_model(const std::string &relative_path,
                                   float voxel_size_meters = 1.0f);
