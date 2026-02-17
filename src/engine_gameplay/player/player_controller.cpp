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

glm::vec3 PlayerControllerSystem::orbit_forward_from_angles(float yaw_deg, float pitch_deg) {
    const float yaw = to_radians(yaw_deg);
    const float pitch = to_radians(pitch_deg);
    return glm::normalize(glm::vec3(
        std::sin(yaw) * std::cos(pitch),
        std::sin(pitch),
        std::cos(yaw) * std::cos(pitch)));
}

MovementDebug PlayerControllerSystem::compute_movement_vectors(float yaw_deg, glm::vec2 move_axis) {
    MovementDebug out{};
    const float yaw_rad = to_radians(yaw_deg);
    out.forward = glm::normalize(glm::vec3(std::sin(yaw_rad), 0.0f, std::cos(yaw_rad)));
    out.right = glm::normalize(glm::cross(out.forward, glm::vec3(0.0f, 1.0f, 0.0f)));
    out.desired = out.forward * move_axis.y + out.right * move_axis.x;
    return out;
}

void PlayerControllerSystem::update_camera_rig(PlayerEntity &player, const InputState &input, bool touch_mode, float dt) {
    const float sensitivity = touch_mode ? player.camera_rig.sensitivityTouch * dt : player.camera_rig.sensitivityMouse;
    player.camera_rig.yaw += input.look_delta.x * sensitivity;
    player.camera_rig.pitch -= input.look_delta.y * sensitivity;

    player.camera_rig.pitch = std::clamp(player.camera_rig.pitch, player.camera_rig.pitchMinDeg, player.camera_rig.pitchMaxDeg);
    player.camera_rig.distance = std::clamp(
        player.camera_rig.distance - input.zoom_delta,
        player.camera_rig.minDistance,
        player.camera_rig.maxDistance);
}

PlayerCollisionDebug PlayerControllerSystem::simulate_fixed(
    PlayerEntity &player,
    const InputState &input,
    const VoxelCollisionWorld &collision_world,
    float dt,
    bool noclip) {
    PlayerCollisionDebug debug{};

    const MovementDebug movement_debug = compute_movement_vectors(player.camera_rig.yaw, input.move);
    glm::vec3 move = movement_debug.desired;
    if (glm::length(move) > 0.001f) {
        move = glm::normalize(move);
    }

    const float speed = input.sprint_held ? player.controller.sprintSpeed : player.controller.walkSpeed;

    if (noclip) {
        player.transform.position += move * speed * dt;
        if (input.jump_held) {
            player.transform.position.y += speed * dt;
        }
        player.controller.grounded = false;
        player.controller.velocity = glm::vec3(0.0f);
        return debug;
    }

    player.controller.velocity.x = move.x * speed;
    player.controller.velocity.z = move.z * speed;

    if (player.controller.grounded && input.jump_pressed) {
        player.controller.velocity.y = player.controller.jumpVelocity;
        player.controller.grounded = false;
    }

    if (!player.controller.grounded) {
        player.controller.velocity.y += player.controller.gravity * dt;
    }

    glm::vec3 next_pos = player.transform.position + player.controller.velocity * dt;
    const CapsuleResolveResult resolve = collision_world.resolve_capsule(
        next_pos,
        player.controller.capsuleRadius,
        player.controller.capsuleHeight,
        0.02f,
        6,
        0.5f);

    player.transform.position = resolve.position;
    player.controller.grounded = resolve.grounded;
    if (resolve.grounded && player.controller.velocity.y < 0.0f) {
        player.controller.velocity.y = 0.0f;
    }

    debug.had_collision = resolve.had_collision;
    debug.contact_normal = resolve.contact_normal;
    debug.penetration_correction = resolve.total_correction;
    debug.grounded = resolve.grounded;
    debug.grounding_ray_origin = resolve.ground_ray_origin;
    debug.grounding_ray_hit = resolve.ground_ray_hit;
    debug.overlapped_voxels = resolve.overlapped_voxels;

    if (glm::length(glm::vec2(move.x, move.z)) > 0.001f) {
        const float facing = std::atan2(move.x, move.z);
        player.transform.rotation = glm::angleAxis(facing, glm::vec3(0.0f, 1.0f, 0.0f));
    }

    return debug;
}
