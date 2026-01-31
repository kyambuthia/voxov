#include "planet_world.hpp"

#include <cmath>

PlanetWorld::PlanetWorld() {}

PlanetWorld::PlanetWorld(glm::vec3 planet_center, float planet_radius_blocks, float planet_surface_thickness_blocks)
    : center(planet_center),
      radius_blocks(planet_radius_blocks),
      surface_thickness_blocks(planet_surface_thickness_blocks) {}

bool PlanetWorld::is_solid(glm::vec3 position_blocks) const {
    const float d = glm::length(position_blocks - center);
    const float inner = radius_blocks - surface_thickness_blocks;

    return (d >= inner) && (d <= radius_blocks);
}
