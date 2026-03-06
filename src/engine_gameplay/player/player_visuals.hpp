#pragma once

#include "engine_gameplay/player/player_components.hpp"
#include "engine_render/render_types.hpp"

#include <cstdint>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

glm::vec3 player_color_from_network_id(uint32_t player_id);
