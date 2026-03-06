#pragma once

#include "engine_gameplay/player/player_components.hpp"
#include "engine_render/render_types.hpp"

#include <cstdint>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

glm::vec3 player_color_from_network_id(uint32_t player_id);
const char *skate_movement_state_name(SkateMovementState state);
const char *skate_trick_state_name(SkateTrickState state);
RenderMesh build_skateboard_mesh(
    const glm::vec3 &feet_position,
    const glm::quat &facing_rotation,
    const SkateboardState &skate_state,
    const glm::vec3 &base_color);
