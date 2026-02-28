#include "engine_gameplay/player/player_visuals.hpp"

glm::vec3 player_color_from_network_id(uint32_t player_id) {
    const uint32_t h = (player_id * 2654435761u) ^ 0x9e3779b9u;
    const float r = 0.25f + 0.65f * static_cast<float>((h >> 0) & 0xFF) / 255.0f;
    const float g = 0.25f + 0.65f * static_cast<float>((h >> 8) & 0xFF) / 255.0f;
    const float b = 0.25f + 0.65f * static_cast<float>((h >> 16) & 0xFF) / 255.0f;
    return glm::vec3(r, g, b);
}
