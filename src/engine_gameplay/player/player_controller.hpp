#pragma once

#include "engine_gameplay/player/player_components.hpp"
#include "engine_input/input_state.hpp"

#include <glm/glm.hpp>

class VoxelCollisionWorld;

struct PlayerCollisionDebug {
    float penetration_correction = 0.0f;
    glm::vec3 contact_normal = glm::vec3(0.0f, 1.0f, 0.0f);
    bool had_collision = false;
};

class PlayerControllerSystem {
public:
    static PlayerEntity spawn_player(const VoxelCollisionWorld &collision_world);

    static void update_camera_rig(PlayerEntity &player, const InputState &input, bool touch_mode, float dt);
    static PlayerCollisionDebug simulate_fixed(
        PlayerEntity &player,
        const InputState &input,
        const VoxelCollisionWorld &collision_world,
        float dt,
        bool noclip);
};
