#pragma once

#include "engine_world/voxel_chunk.hpp"

#include <array>
#include <cstddef>
#include <glm/glm.hpp>

// Placeable block palette, selected with the number keys.
struct BlockHotbarEntry {
  VoxelMaterial material;
  const char *label;
  glm::vec3 swatch; // average tile colour for the HUD slot
};

inline constexpr std::array<BlockHotbarEntry, 7> kBlockHotbar{{
    {VoxelMaterial::Stone, "STONE", {0.34f, 0.34f, 0.37f}},
    {VoxelMaterial::Dirt, "DIRT", {0.36f, 0.23f, 0.15f}},
    {VoxelMaterial::Grass, "GRASS", {0.41f, 0.49f, 0.13f}},
    {VoxelMaterial::Sand, "SAND", {0.80f, 0.72f, 0.50f}},
    {VoxelMaterial::Planks, "PLANKS", {0.57f, 0.39f, 0.21f}},
    {VoxelMaterial::Brick, "BRICK", {0.61f, 0.39f, 0.33f}},
    {VoxelMaterial::Snow, "SNOW", {0.89f, 0.92f, 0.95f}},
}};

inline constexpr std::size_t kBlockHotbarSize = kBlockHotbar.size();
