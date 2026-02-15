#include "engine_gameplay/player/player_controller.hpp"

#include "engine_world/physics/voxel_collision.hpp"

#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace {
float to_radians(float deg) {
    return deg * 0.01745329251994329577f;
}
}

PlayerEntity PlayerControllerSystem::spawn_player(const VoxelCollisionWorld &collision_world) {
    PlayerEntity player{};
    player.network_id = 1;
    player.transform.position.x = 8.0f;
    player.transform.position.z = 8.0f;
    player.transform.position.y = collision_world.find_spawn_height(
        glm::vec2(player.transform.position.x, player.transform.position.z),
        player.controller.capsuleRadius,
        player.controller.capsuleHeight);
    return player;
}

void PlayerControllerSystem::update_camera_rig(PlayerEntity &player, const InputState &input, bool touch_mode, float dt) {
    const float sensitivity = touch_mode ? player.camera_rig.sensitivityTouch * dt : player.camera_rig.sensitivityMouse;
    player.camera_rig.yaw += input.look_delta.x * sensitivity;
    player.camera_rig.pitch += input.look_delta.y * sensitivity;

    player.camera_rig.pitch = std::clamp(player.camera_rig.pitch, player.camera_rig.pitchMinDeg, player.camera_rig.pitchMaxDeg);
    player.camera_rig.distance = std::clamp(
        player.camera_rig.distance - input.zoom_delta,
        player.camera_rig.minDistance,
        player.camera_rig.maxDistance);
}

void PlayerControllerSystem::simulate_fixed(
    PlayerEntity &player,
    const InputState &input,
    const VoxelCollisionWorld &collision_world,
    float dt) {
    const float yaw_rad = to_radians(player.camera_rig.yaw);
    const glm::vec3 cam_fwd = glm::normalize(glm::vec3(std::sin(yaw_rad), 0.0f, std::cos(yaw_rad)));
    const glm::vec3 cam_right = glm::normalize(glm::cross(cam_fwd, glm::vec3(0.0f, 1.0f, 0.0f)));

    glm::vec3 move = cam_fwd * input.move.y + cam_right * input.move.x;
    if (glm::length(move) > 0.001f) {
        move = glm::normalize(move);
    }

    const float speed = input.sprint_held ? player.controller.sprintSpeed : player.controller.walkSpeed;
    glm::vec3 horizontal = move * speed;

    player.controller.velocity.x = horizontal.x;
    player.controller.velocity.z = horizontal.z;

    if (player.controller.grounded && input.jump_pressed) {
        player.controller.velocity.y = player.controller.jumpVelocity;
        player.controller.grounded = false;
    }

    player.controller.velocity.y += player.controller.gravity * dt;

    glm::vec3 pos = player.transform.position;

    auto move_axis = [&](int axis, float amount) {
        if (std::fabs(amount) < 0.00001f) {
            return;
        }

        glm::vec3 next = pos;
        next[axis] += amount;
        if (!collision_world.capsule_overlaps(next, player.controller.capsuleRadius, player.controller.capsuleHeight)) {
            pos = next;
        } else {
            player.controller.velocity[axis] = 0.0f;
        }
    };

    move_axis(0, player.controller.velocity.x * dt);
    move_axis(2, player.controller.velocity.z * dt);
    move_axis(1, player.controller.velocity.y * dt);

    const glm::vec3 probe = pos + glm::vec3(0.0f, -0.05f, 0.0f);
    player.controller.grounded = collision_world.capsule_overlaps(
        probe,
        player.controller.capsuleRadius,
        player.controller.capsuleHeight);

    if (player.controller.grounded && player.controller.velocity.y < 0.0f) {
        player.controller.velocity.y = 0.0f;
    }

    player.transform.position = pos;

    if (glm::length(glm::vec2(move.x, move.z)) > 0.001f) {
        const float facing = std::atan2(move.x, move.z);
        player.transform.rotation = glm::angleAxis(facing, glm::vec3(0.0f, 1.0f, 0.0f));
    }
}
