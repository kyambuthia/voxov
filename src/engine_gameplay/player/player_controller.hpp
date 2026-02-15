#pragma once

#include "engine_gameplay/player/player_components.hpp"
#include "engine_input/input_state.hpp"

class VoxelCollisionWorld;

class PlayerControllerSystem {
public:
    static PlayerEntity spawn_player(const VoxelCollisionWorld &collision_world);

    static void update_camera_rig(PlayerEntity &player, const InputState &input, bool touch_mode, float dt);
    static void simulate_fixed(PlayerEntity &player, const InputState &input, const VoxelCollisionWorld &collision_world, float dt);
};
