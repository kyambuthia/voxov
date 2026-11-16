#pragma once

#include "engine_world/physics/voxel_collision.hpp"
#include "engine_world/planet_blocks.hpp"
#include "engine_world/solar_system.hpp"

#include <cstdint>

// Derives the deterministic BlockWorldConfig for any non-star body in
// SolarSystem from the base Voxov terrain profile.
BlockWorldConfig make_body_block_world_config(const BlockWorldConfig &base_config,
                                              const SolarSystem &solar_system,
                                              int32_t body_index);

// Installs a terrain-height-aware radial surface collider backed by
// block_world. The caller must keep block_world alive as long as
// collision_world queries it.
void install_planet_surface_collider(VoxelCollisionWorld &collision_world,
                                     const BlockWorld &block_world);
