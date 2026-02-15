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

struct MovementDebug {
    glm::vec3 forward = glm::vec3(0.0f, 0.0f, 1.0f);
    glm::vec3 right = glm::vec3(1.0f, 0.0f, 0.0f);
    glm::vec3 desired = glm::vec3(0.0f);
};

class PlayerControllerSystem {
public:
    static PlayerEntity spawn_player(const VoxelCollisionWorld &collision_world);
    static glm::vec3 orbit_forward_from_angles(float yaw_deg, float pitch_deg);
    static MovementDebug compute_movement_vectors(float yaw_deg, glm::vec2 move_axis);

    static void update_camera_rig(PlayerEntity &player, const InputState &input, bool touch_mode, float dt);
    static PlayerCollisionDebug simulate_fixed(
        PlayerEntity &player,
        const InputState &input,
        const VoxelCollisionWorld &collision_world,
        float dt,
        bool noclip);
};
