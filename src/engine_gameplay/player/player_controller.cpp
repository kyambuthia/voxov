#include "engine_gameplay/player/player_controller.hpp"

#include "engine_world/physics/voxel_collision.hpp"
#include "engine_world/voxel_chunk.hpp"

#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
float to_radians(float deg) {
    return deg * 0.01745329251994329577f;
}

int top_solid_y(const VoxelCollisionWorld &collision_world, int x, int z) {
    for (int y = VoxelChunk::CHUNK_Y - 1; y >= 0; --y) {
        if (collision_world.is_solid_voxel(x, y, z)) {
            return y;
        }
    }
    return -1;
}

bool has_flat_patch(const VoxelCollisionWorld &collision_world, int cx, int cz, int expected_top_y) {
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            const int x = cx + dx;
            const int z = cz + dz;
            if (x < 0 || z < 0 || x >= VoxelChunk::CHUNK_X || z >= VoxelChunk::CHUNK_Z) {
                return false;
            }
            if (top_solid_y(collision_world, x, z) != expected_top_y) {
                return false;
            }
            if (collision_world.is_solid_voxel(x, expected_top_y + 1, z)) {
                return false;
            }
        }
    }
    return true;
}

glm::vec2 find_flat_spawn_xz(const VoxelCollisionWorld &collision_world, glm::vec2 preferred) {
    const int preferred_x = static_cast<int>(std::clamp(std::floor(preferred.x), 1.0f, static_cast<float>(VoxelChunk::CHUNK_X - 2)));
    const int preferred_z = static_cast<int>(std::clamp(std::floor(preferred.y), 1.0f, static_cast<float>(VoxelChunk::CHUNK_Z - 2)));

    float best_dist_sq = std::numeric_limits<float>::max();
    glm::vec2 best = glm::vec2(static_cast<float>(preferred_x) + 0.5f, static_cast<float>(preferred_z) + 0.5f);

    for (int radius = 0; radius <= 6; ++radius) {
        for (int z = preferred_z - radius; z <= preferred_z + radius; ++z) {
            for (int x = preferred_x - radius; x <= preferred_x + radius; ++x) {
                if (x < 1 || z < 1 || x >= VoxelChunk::CHUNK_X - 1 || z >= VoxelChunk::CHUNK_Z - 1) {
                    continue;
                }
                const int top_y = top_solid_y(collision_world, x, z);
                if (top_y < 0) {
                    continue;
                }
                if (!has_flat_patch(collision_world, x, z, top_y)) {
                    continue;
                }

                const float dx = static_cast<float>(x - preferred_x);
                const float dz = static_cast<float>(z - preferred_z);
                const float dist_sq = dx * dx + dz * dz;
                if (dist_sq < best_dist_sq) {
                    best_dist_sq = dist_sq;
                    best = glm::vec2(static_cast<float>(x) + 0.5f, static_cast<float>(z) + 0.5f);
                }
            }
        }
        if (best_dist_sq < std::numeric_limits<float>::max()) {
            break;
        }
    }

    return best;
}

}

float player_anim_cycle_rate(PlayerAnimState state) {
    switch (state) {
    case PlayerAnimState::Walk:
        return 5.0f;
    case PlayerAnimState::Run:
        return 8.0f;
    case PlayerAnimState::Crawl:
        return 2.8f;
    case PlayerAnimState::Jump:
        return 3.0f;
    case PlayerAnimState::Idle:
    default:
        return 1.0f;
    }
}

float player_anim_blend_target(PlayerAnimState state) {
    switch (state) {
    case PlayerAnimState::Walk:
        return 0.5f;
    case PlayerAnimState::Run:
        return 1.0f;
    case PlayerAnimState::Crawl:
        return 0.35f;
    case PlayerAnimState::Jump:
        return 0.75f;
    case PlayerAnimState::Idle:
    default:
        return 0.0f;
    }
}

PlayerEntity PlayerControllerSystem::spawn_player(const VoxelCollisionWorld &collision_world) {
    PlayerEntity player{};
    player.network_id = 1;
    const glm::vec2 spawn_xz = find_flat_spawn_xz(collision_world, glm::vec2(8.0f, 8.0f));
    player.transform.position.x = spawn_xz.x;
    player.transform.position.z = spawn_xz.y;
    player.transform.position.y = collision_world.find_spawn_height(
        glm::vec2(player.transform.position.x, player.transform.position.z),
        player.controller.capsuleRadius,
        player.controller.capsuleHeight);
    player.transform.position.y += 0.05f;
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
    const bool was_grounded = player.controller.grounded;

    const MovementDebug movement_debug = compute_movement_vectors(player.camera_rig.yaw, input.move);
    glm::vec3 move = movement_debug.desired;
    if (glm::length(move) > 0.001f) {
        move = glm::normalize(move);
    }

    float speed = player.controller.walkSpeed;
    if (input.crouch_held) {
        speed = player.controller.crawlSpeed;
    } else if (input.sprint_held) {
        speed = player.controller.sprintSpeed;
    }

    if (noclip) {
        player.transform.position += move * speed * dt;
        if (input.jump_held) {
            player.transform.position.y += speed * dt;
        }
        player.controller.grounded = false;
        player.controller.velocity = glm::vec3(0.0f);
        update_animation_state(player, input, dt, noclip);
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
    CapsuleResolveResult resolve = collision_world.resolve_capsule(
        next_pos,
        player.controller.capsuleRadius,
        player.controller.capsuleHeight,
        0.02f,
        8,
        1.2f);

    if (was_grounded && glm::length(glm::vec2(move.x, move.z)) > 0.001f && resolve.had_collision) {
        const float step_height = 0.65f;
        glm::vec3 step_test = player.transform.position;
        step_test.x += move.x * speed * dt;
        step_test.z += move.z * speed * dt;
        step_test.y += step_height;

        CapsuleResolveResult step_resolve = collision_world.resolve_capsule(
            step_test,
            player.controller.capsuleRadius,
            player.controller.capsuleHeight,
            0.02f,
            8,
            1.2f);

        float step_hit_distance = 0.0f;
        const glm::vec3 step_origin = step_resolve.position + glm::vec3(0.0f, 0.12f, 0.0f);
        if (collision_world.raycast(step_origin, glm::vec3(0.0f, -1.0f, 0.0f), step_height + 0.25f, step_hit_distance)) {
            step_resolve.position.y = step_origin.y - step_hit_distance + 0.02f;
            step_resolve.grounded = true;
        }

        const float base_progress = glm::length(glm::vec2(
            resolve.position.x - player.transform.position.x,
            resolve.position.z - player.transform.position.z));
        const float step_progress = glm::length(glm::vec2(
            step_resolve.position.x - player.transform.position.x,
            step_resolve.position.z - player.transform.position.z));
        if (step_progress > base_progress + 0.01f) {
            resolve = step_resolve;
        }
    }

    player.transform.position = resolve.position;
    player.controller.grounded = resolve.grounded;
    if (resolve.grounded && player.controller.velocity.y < 0.0f) {
        player.controller.velocity.y = 0.0f;
    }

    if (!resolve.grounded && was_grounded && !input.jump_pressed && player.controller.velocity.y <= 0.0f) {
        float snap_hit_distance = 0.0f;
        const glm::vec3 snap_origin = player.transform.position + glm::vec3(0.0f, 0.10f, 0.0f);
        const float max_snap_distance = 0.90f;
        if (collision_world.raycast(snap_origin, glm::vec3(0.0f, -1.0f, 0.0f), max_snap_distance, snap_hit_distance)) {
            player.transform.position.y = snap_origin.y - snap_hit_distance + 0.02f;
            player.controller.grounded = true;
            player.controller.velocity.y = 0.0f;
        }
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

    update_animation_state(player, input, dt, noclip);

    return debug;
}

void PlayerControllerSystem::update_animation_state(PlayerEntity &player, const InputState &input, float dt, bool noclip) {
    const float horizontal_speed = glm::length(glm::vec2(player.controller.velocity.x, player.controller.velocity.z));
    const bool moving = glm::length(input.move) > 0.12f || horizontal_speed > 0.18f;
    const float speed_ratio = std::clamp(horizontal_speed / std::max(0.001f, player.controller.sprintSpeed), 0.0f, 1.0f);

    PlayerAnimState next_state = PlayerAnimState::Idle;
    if (!noclip && (!player.controller.grounded || std::fabs(player.controller.velocity.y) > 0.15f)) {
        next_state = PlayerAnimState::Jump;
    } else if (moving) {
        if (input.crouch_held) {
            next_state = PlayerAnimState::Crawl;
        } else if (input.sprint_held) {
            next_state = PlayerAnimState::Run;
        } else {
            next_state = PlayerAnimState::Walk;
        }
    }

    const PlayerAnimState prev_state = player.anim_state;
    player.anim_state = next_state;
    if (prev_state != player.anim_state) {
        // Keep leg cycle continuous but avoid sudden offset on state transitions.
        player.anim_phase = std::fmod(player.anim_phase * 0.6f, 6.28318530718f);
    }
    player.anim_phase += player_anim_cycle_rate(player.anim_state) * dt;
    if (player.anim_phase > 6.28318530718f) {
        player.anim_phase = std::fmod(player.anim_phase, 6.28318530718f);
    }

    float target_blend = player_anim_blend_target(player.anim_state);
    if (player.anim_state == PlayerAnimState::Walk || player.anim_state == PlayerAnimState::Run) {
        target_blend = std::clamp(target_blend * (0.55f + speed_ratio * 0.9f), 0.0f, 1.0f);
    }
    const float blend_step = std::clamp(10.0f * dt, 0.0f, 1.0f);
    player.anim_blend += (target_blend - player.anim_blend) * blend_step;
}
