#include "engine_world/planet_world_config.hpp"

#include "engine_world/world_gen.hpp"

BlockWorldConfig make_body_block_world_config(const BlockWorldConfig &base_config,
                                              const SolarSystem &solar_system,
                                              int32_t body_index) {
  BlockWorldConfig config = base_config;
  if (body_index == kVoxovBodyIndex || body_index < 0 ||
      body_index >= solar_system.body_count()) {
    return config;
  }
  config.planet.radius =
      solar_system.bodies()[static_cast<size_t>(body_index)].orbital.radius;
  // Aster keeps its original seed so existing saves regenerate the same
  // terrain under their recorded edits.
  config.planet.seed = body_index == kAsterBodyIndex
      ? (k_voxov_flat_world_seed ^ 0xa57ec0deull)
      : (k_voxov_flat_world_seed ^
         (0x9e3779b97f4a7c15ull * static_cast<uint64_t>(body_index)));
  config.seed = config.planet.seed;
  return config;
}

void install_planet_surface_collider(VoxelCollisionWorld &collision_world,
                                     const BlockWorld &block_world) {
  const BlockWorld *world_ptr = &block_world;
  collision_world.set_planet_surface_collider(
      glm::vec3(0.0f), static_cast<float>(block_world.planet().radius),
      static_cast<float>(block_world.max_surface_height_above_base()),
      [world_ptr](glm::vec3 direction) -> float {
        return static_cast<float>(
            world_ptr->surface_height_above_base(glm::dvec3(direction)));
      });
}
