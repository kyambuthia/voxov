#pragma once

#include "engine_world/planet_blocks.hpp"

#include <glm/glm.hpp>

#include <optional>
#include <vector>

// Crosshair block picking shared by gameplay and tests.
struct BlockPick {
  std::optional<BlockAddress> hit;   // first solid block along the ray
  std::optional<BlockAddress> place; // empty cell crossed just before `hit`
  glm::vec3 face_normal{0.0f};       // world direction from hit to place
};

// Marches the ray from `ray_origin` up to `max_distance`. Hits farther than
// `reach` from `reach_origin` (the player's eye) are rejected, so a
// third-person camera neither extends nor shortens the player's reach.
// Unloaded chunks break the empty-cell chain, so `place` is always a cell
// in a resident chunk adjacent to the hit along the ray.
BlockPick pick_block(const BlockWorld &world, const glm::vec3 &ray_origin,
                     const glm::vec3 &ray_dir, const glm::vec3 &reach_origin,
                     float reach, float max_distance);

// True when the block's inscribed sphere intersects the upright capsule
// standing at `feet` along the planet's radial up.
bool block_overlaps_capsule(const BlockWorld &world, const BlockAddress &block,
                            const glm::dvec3 &feet, double radius,
                            double height);

// Chunk keys whose meshes depend on `block`: its own chunk plus the
// neighbours across every chunk face, edge, or corner the block touches.
std::vector<BlockAddress> chunks_touching_block(const BlockWorld &world,
                                                const BlockAddress &block);
