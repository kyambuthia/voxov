#pragma once

#include <glm/glm.hpp>

class PlanetWorld {
public:
    PlanetWorld();
    PlanetWorld(glm::vec3 center, float radius_blocks, float surface_thickness_blocks);

    bool is_solid(glm::vec3 position_blocks) const;

private:
    glm::vec3 center = glm::vec3(0.0f);
    float radius_blocks = 0.0f;
    float surface_thickness_blocks = 0.0f;
};
