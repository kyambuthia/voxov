#include "engine_gameplay/player/player_controller.hpp"

#include "engine_gameplay/animation/player_animation_graph.hpp"
#include "engine_world/physics/voxel_collision.hpp"
#include "engine_world/voxel_chunk.hpp"

#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr float k_pi = 3.14159265358979323846f;
constexpr float k_tau = 6.28318530718f;

float to_radians(float deg) {
    return deg * (k_pi / 180.0f);
}

float to_degrees(float rad) {
    return rad * (180.0f / k_pi);
}

float wrap_degrees(float deg) {
    while (deg > 180.0f) {
        deg -= 360.0f;
    }
    while (deg < -180.0f) {
        deg += 360.0f;
    }
    return deg;
}

float angle_delta_deg(float from_deg, float to_deg) {
    return wrap_degrees(to_deg - from_deg);
}

float rotate_towards_deg(float current_deg, float target_deg, float max_delta_deg) {
    const float delta = angle_delta_deg(current_deg, target_deg);
    if (std::fabs(delta) <= max_delta_deg) {
        return target_deg;
    }
    return current_deg + std::copysign(max_delta_deg, delta);
}

glm::vec3 move_towards_vec3(glm::vec3 current, glm::vec3 target, float max_delta) {
    const glm::vec3 delta = target - current;
    const float len = glm::length(delta);
    if (len <= max_delta || len <= 1.0e-5f) {
        return target;
    }
    return current + (delta / len) * max_delta;
}

float flat_length(glm::vec3 v) {
    return glm::length(glm::vec2(v.x, v.z));
}

glm::vec3 tangent_or_fallback(glm::vec3 value, glm::vec3 up, glm::vec3 fallback) {
    value -= up * glm::dot(value, up);
    const float len = glm::length(value);
    if (len > 1.0e-5f) {
        return value / len;
    }

    fallback -= up * glm::dot(fallback, up);
    const float fallback_len = glm::length(fallback);
    if (fallback_len > 1.0e-5f) {
        return fallback / fallback_len;
    }

    return glm::vec3(1.0f, 0.0f, 0.0f);
}

glm::quat orientation_from_frame(glm::vec3 forward, glm::vec3 up) {
    up = glm::normalize(up);
    forward = tangent_or_fallback(forward, up, glm::vec3(0.0f, 0.0f, 1.0f));
    const glm::vec3 right = glm::normalize(glm::cross(forward, up));
    forward = glm::normalize(glm::cross(up, right));
    return glm::quat_cast(glm::mat3(right, up, forward));
}

float move_direction_deg(glm::vec2 move_axis) {
    if (glm::length(move_axis) <= 1.0e-5f) {
        return 0.0f;
    }
    return wrap_degrees(to_degrees(std::atan2(move_axis.x, move_axis.y)));
}

int top_solid_y(const VoxelCollisionWorld &collision_world, int x, int z) {
    for (int y = VoxelChunk::CHUNK_Y - 1; y >= 0; --y) {
        if (collision_world.is_solid_voxel(x, y, z)) {
            return y;
        }
    }
    return -1;
}

float sample_surface_height(const VoxelCollisionWorld &collision_world, int x, int z) {
    return static_cast<float>(top_solid_y(collision_world, x, z) + 1);
}

glm::vec3 estimate_ground_normal(const VoxelCollisionWorld &collision_world, glm::vec3 feet_position) {
    if (collision_world.has_planet_surface_collider()) {
        return collision_world.planet_up_at(feet_position);
    }

    const int x = static_cast<int>(std::floor(feet_position.x));
    const int z = static_cast<int>(std::floor(feet_position.z));
    const float h_l = sample_surface_height(collision_world, x - 1, z);
    const float h_r = sample_surface_height(collision_world, x + 1, z);
    const float h_d = sample_surface_height(collision_world, x, z - 1);
    const float h_u = sample_surface_height(collision_world, x, z + 1);
    glm::vec3 normal(h_l - h_r, 2.0f, h_d - h_u);
    if (glm::length(normal) <= 1.0e-5f) {
        return glm::vec3(0.0f, 1.0f, 0.0f);
    }
    return glm::normalize(normal);
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

void set_locomotion_state(PlayerEntity &player, PlayerLocomotionState next_state) {
    if (player.locomotion.state != next_state) {
        player.locomotion.state = next_state;
        player.locomotion.state_timer = 0.0f;
    }
}

CapsuleResolveResult simulate_capsule(
    PlayerEntity &player,
    const VoxelCollisionWorld &collision_world,
    glm::vec3 desired_flat_velocity,
    glm::vec3 up,
    float dt) {
    const LocomotionTuningData &tuning = player.locomotion_tuning;
    glm::vec3 next_pos = player.transform.position + desired_flat_velocity * dt;
    next_pos += up * (player.locomotion.vertical_velocity * dt);

    CapsuleResolveResult resolve = collision_world.resolve_capsule(
        next_pos,
        player.controller.capsuleRadius,
        player.controller.capsuleHeight,
        0.02f,
        8,
        1.2f);

    if (player.controller.grounded && glm::length(desired_flat_velocity) > 0.001f && resolve.had_collision) {
        glm::vec3 step_lift = player.transform.position;
        step_lift += up * tuning.step_offset;
        CapsuleResolveResult step_lift_resolve = collision_world.resolve_capsule(
            step_lift,
            player.controller.capsuleRadius,
            player.controller.capsuleHeight,
            0.02f,
            8,
            1.2f);

        glm::vec3 step_test = step_lift_resolve.position + desired_flat_velocity * dt;

        CapsuleResolveResult step_resolve = collision_world.resolve_capsule(
            step_test,
            player.controller.capsuleRadius,
            player.controller.capsuleHeight,
            0.02f,
            8,
            1.2f);

        float step_hit_distance = 0.0f;
        const glm::vec3 step_up = collision_world.has_planet_surface_collider()
                                      ? collision_world.planet_up_at(step_resolve.position)
                                      : up;
        const glm::vec3 step_origin = step_resolve.position + step_up * 0.12f;
        if (collision_world.raycast(step_origin, -step_up, tuning.step_offset + 0.25f, step_hit_distance)) {
            step_resolve.position = step_origin - step_up * step_hit_distance + step_up * 0.02f;
            step_resolve.grounded = true;
        }

        const glm::vec3 base_delta = resolve.position - player.transform.position;
        const glm::vec3 step_delta = step_resolve.position - player.transform.position;
        const float base_progress =
            glm::length(base_delta - up * glm::dot(base_delta, up));
        const float step_progress =
            glm::length(step_delta - up * glm::dot(step_delta, up));
        const float step_height_gain = glm::dot(step_resolve.position - player.transform.position, up);
        if (step_progress > base_progress + 0.01f ||
            (step_height_gain > 0.05f && step_progress > 0.01f)) {
            resolve = step_resolve;
        }
    }

    return resolve;
}

PlayerAnimState map_animation_state(const PlayerEntity &player, const InputState &input, bool grounded_last_frame) {
    (void)grounded_last_frame;
    const PlayerLocomotionState state = player.locomotion.state;
    const float yaw_delta = player.locomotion.turn_delta_deg;

    switch (state) {
    case PlayerLocomotionState::StartMove:
        return PlayerAnimState::StartMove;
    case PlayerLocomotionState::StopMove:
        return PlayerAnimState::StopMove;
    case PlayerLocomotionState::Walk:
        return player_anim_directional_locomotion_state(
            false, player.locomotion.move_direction_deg);
    case PlayerLocomotionState::Run:
        return player_anim_directional_locomotion_state(
            true, player.locomotion.move_direction_deg);
    case PlayerLocomotionState::JumpStart:
        return PlayerAnimState::JumpTakeoff;
    case PlayerLocomotionState::AirborneRise:
        return PlayerAnimState::JumpLoop;
    case PlayerLocomotionState::AirborneFall:
        return PlayerAnimState::FallLoop;
    case PlayerLocomotionState::LandSoft:
        return PlayerAnimState::LandSoft;
    case PlayerLocomotionState::LandHard:
        return PlayerAnimState::LandHard;
    case PlayerLocomotionState::TurnInPlace:
        return yaw_delta < 0.0f ? PlayerAnimState::TurnInPlaceLeft : PlayerAnimState::TurnInPlaceRight;
    case PlayerLocomotionState::MovingTurn:
        if (flat_length(player.locomotion.planar_velocity) < player.locomotion_tuning.walk_speed * 0.45f) {
            return yaw_delta < 0.0f ? PlayerAnimState::PivotLeft : PlayerAnimState::PivotRight;
        }
        return PlayerAnimState::MovingTurn;
    case PlayerLocomotionState::Slide:
    case PlayerLocomotionState::Recovery:
        return PlayerAnimState::Recovery;
    case PlayerLocomotionState::Idle:
    default:
        if (std::fabs(yaw_delta) > player.locomotion_tuning.pivot_threshold_deg && std::fabs(input.look_delta.x) > 0.01f) {
            return yaw_delta < 0.0f ? PlayerAnimState::TurnInPlaceLeft : PlayerAnimState::TurnInPlaceRight;
        }
        return PlayerAnimState::Idle;
    }
}
}

const char *player_locomotion_state_name(PlayerLocomotionState state) {
    switch (state) {
    case PlayerLocomotionState::Idle:
        return "IDLE";
    case PlayerLocomotionState::StartMove:
        return "START_MOVE";
    case PlayerLocomotionState::Walk:
        return "WALK";
    case PlayerLocomotionState::Run:
        return "RUN";
    case PlayerLocomotionState::StopMove:
        return "STOP_MOVE";
    case PlayerLocomotionState::JumpStart:
        return "JUMP_START";
    case PlayerLocomotionState::AirborneRise:
        return "AIR_RISE";
    case PlayerLocomotionState::AirborneFall:
        return "AIR_FALL";
    case PlayerLocomotionState::LandSoft:
        return "LAND_SOFT";
    case PlayerLocomotionState::LandHard:
        return "LAND_HARD";
    case PlayerLocomotionState::TurnInPlace:
        return "TURN_IN_PLACE";
    case PlayerLocomotionState::MovingTurn:
        return "MOVING_TURN";
    case PlayerLocomotionState::Slide:
        return "SLIDE";
    case PlayerLocomotionState::Recovery:
        return "RECOVERY";
    default:
        return "UNK";
    }
}

float player_anim_cycle_rate(PlayerAnimState state) {
    return player_animation_definition(state).phase_rate;
}

float player_anim_blend_target(PlayerAnimState state) {
    return player_animation_definition(state).target_blend;
}

float player_anim_crossfade_seconds(PlayerAnimState state) {
    return player_animation_definition(state).crossfade_seconds;
}

PlayerEntity PlayerControllerSystem::spawn_player(const VoxelCollisionWorld &collision_world) {
    PlayerEntity player{};
    player.network_id = 1;
    const glm::vec2 preferred_spawn(
        static_cast<float>(VoxelChunk::CHUNK_X) * 0.5f,
        static_cast<float>(VoxelChunk::CHUNK_Z) * 0.5f);
    const glm::vec2 spawn_xz = find_flat_spawn_xz(collision_world, preferred_spawn);
    player.transform.position.x = spawn_xz.x;
    player.transform.position.z = spawn_xz.y;
    const float terrain_spawn_y = collision_world.find_spawn_height(
        glm::vec2(player.transform.position.x, player.transform.position.z),
        player.controller.capsuleRadius,
        player.controller.capsuleHeight) + 0.05f;
    player.transform.position.y = std::max(2.0f, terrain_spawn_y);
    player.locomotion.facing_yaw_deg = player.camera_rig.yaw;
    player.locomotion.desired_yaw_deg = player.camera_rig.yaw;
    player.transform.rotation = glm::angleAxis(to_radians(player.locomotion.facing_yaw_deg), glm::vec3(0.0f, 1.0f, 0.0f));
    player.animation.state = player.anim_state;
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
    LocomotionTuningData &tuning = player.locomotion_tuning;
    PlayerLocomotionStateData &motion = player.locomotion;
    const glm::vec3 up = collision_world.has_planet_surface_collider()
                             ? collision_world.planet_up_at(player.transform.position)
                             : glm::vec3(0.0f, 1.0f, 0.0f);
    MovementDebug movement_debug = compute_movement_vectors(player.camera_rig.yaw, input.move);
    if (collision_world.has_planet_surface_collider()) {
        // Build tangent basis matching the first-person camera's frame.
        // WHY: the camera uses world_ref=(0,1,0) with pole fallback to (0,0,1),
        // then east=cross(world_ref, up), north=cross(up, east).
        // Movement must use the same basis so WASD directions match the view.
        glm::vec3 world_ref = glm::vec3(0.0f, 1.0f, 0.0f);
        if (std::abs(glm::dot(up, world_ref)) > 0.99f) {
            world_ref = glm::vec3(0.0f, 0.0f, 1.0f);
        }
        const glm::vec3 east = glm::normalize(glm::cross(world_ref, up));
        const glm::vec3 north = glm::normalize(glm::cross(up, east));

        const float yaw_rad = to_radians(player.camera_rig.yaw);
        movement_debug.forward =
            glm::normalize(north * std::cos(yaw_rad) + east * std::sin(yaw_rad));
        movement_debug.right = glm::normalize(glm::cross(movement_debug.forward, up));
        movement_debug.desired =
            movement_debug.forward * input.move.y + movement_debug.right * input.move.x;
    }
    glm::vec3 desired_move = movement_debug.desired;
    const float input_len = glm::length(glm::vec2(input.move.x, input.move.y));
    motion.input_magnitude = std::clamp(input_len, 0.0f, 1.0f);
    const bool has_move_input = motion.input_magnitude > tuning.input_deadzone;
    motion.move_direction_deg = has_move_input ? move_direction_deg(input.move) : 0.0f;
    if (has_move_input) {
        desired_move = glm::normalize(desired_move);
    }
    motion.desired_yaw_deg = player.camera_rig.yaw;
    motion.turn_delta_deg = angle_delta_deg(motion.facing_yaw_deg, motion.desired_yaw_deg);

    if (input.jump_pressed) {
        motion.jump_buffer_timer = tuning.jump_buffer_time;
    } else {
        motion.jump_buffer_timer = std::max(0.0f, motion.jump_buffer_timer - dt);
    }

    motion.just_landed = false;
    motion.state_timer += dt;
    motion.landing_impact = 0.0f;

    const bool was_grounded = player.controller.grounded;
    if (was_grounded) {
        motion.coyote_timer = tuning.coyote_time;
    } else {
        motion.coyote_timer = std::max(0.0f, motion.coyote_timer - dt);
    }

    if (noclip) {
        (void)tuning;
        const float noclip_speed = input.sprint_held ? 500000.0f : 80.0f;
        player.transform.position += (has_move_input ? desired_move : glm::vec3(0.0f)) * noclip_speed * dt;
        if (input.jump_held) {
            player.transform.position += up * (noclip_speed * dt);
        }
        if (input.crouch_held) {
            player.transform.position -= up * (noclip_speed * dt);
        }
        player.controller.grounded = false;
        player.controller.velocity = glm::vec3(0.0f);
        motion.planar_velocity = glm::vec3(0.0f);
        motion.move_speed = 0.0f;
        set_locomotion_state(player, PlayerLocomotionState::AirborneFall);
        update_animation_state(player, input, dt, noclip, was_grounded);
        return debug;
    }

    const bool run_intent = input.sprint_held;
    const float desired_speed = has_move_input ? (run_intent ? tuning.run_speed : tuning.walk_speed) * motion.input_magnitude : 0.0f;
    const glm::vec3 desired_planar_velocity = has_move_input ? desired_move * desired_speed : glm::vec3(0.0f);
    const float facing_turn_rate = was_grounded ? tuning.turn_rate : tuning.turn_rate * 0.75f;
    if (has_move_input) {
        motion.facing_yaw_deg = rotate_towards_deg(
            motion.facing_yaw_deg,
            motion.desired_yaw_deg,
            facing_turn_rate * dt);
    } else if (was_grounded && std::fabs(angle_delta_deg(motion.facing_yaw_deg, motion.desired_yaw_deg)) > tuning.pivot_threshold_deg) {
        motion.facing_yaw_deg = rotate_towards_deg(motion.facing_yaw_deg, player.camera_rig.yaw, tuning.turn_rate * 0.55f * dt);
    }
    motion.turn_delta_deg = angle_delta_deg(motion.facing_yaw_deg, motion.desired_yaw_deg);

    if (was_grounded) {
        const float accel = has_move_input ? tuning.ground_accel : tuning.ground_decel;
        motion.planar_velocity = move_towards_vec3(motion.planar_velocity, desired_planar_velocity, accel * dt);
    } else {
        motion.planar_velocity = move_towards_vec3(motion.planar_velocity, desired_planar_velocity, tuning.air_accel * dt);
        motion.vertical_velocity -= tuning.gravity * (motion.vertical_velocity < 0.0f ? tuning.fall_multiplier : 1.0f) * dt;
        if (!input.jump_held && motion.vertical_velocity > 0.0f) {
            motion.vertical_velocity -= tuning.gravity * (tuning.jump_cut_gravity_multiplier - 1.0f) * dt;
            motion.jump_cut_applied = true;
        }
    }

    const bool can_jump =
        motion.jump_buffer_timer > 0.0f &&
        (was_grounded || motion.coyote_timer > 0.0f) &&
        motion.state != PlayerLocomotionState::Recovery;
    if (can_jump) {
        motion.jump_buffer_timer = 0.0f;
        motion.coyote_timer = 0.0f;
        motion.vertical_velocity = tuning.jump_velocity;
        motion.jump_cut_applied = false;
        set_locomotion_state(player, PlayerLocomotionState::JumpStart);
        player.controller.grounded = false;
    }

    const glm::vec3 start_position = player.transform.position;
    const float pre_solve_vertical = motion.vertical_velocity;
    CapsuleResolveResult resolve =
        simulate_capsule(player, collision_world, motion.planar_velocity, up, dt);
    player.transform.position = resolve.position;
    player.controller.grounded = resolve.grounded;
    if (motion.state == PlayerLocomotionState::JumpStart && motion.vertical_velocity > 0.0f) {
        player.controller.grounded = false;
        resolve.grounded = false;
    }

    motion.ground_normal = player.controller.grounded
        ? estimate_ground_normal(collision_world, player.transform.position)
        : up;
    const glm::vec3 slope_up = collision_world.has_planet_surface_collider()
                                   ? collision_world.planet_up_at(player.transform.position)
                                   : glm::vec3(0.0f, 1.0f, 0.0f);
    motion.slope_angle_deg = glm::degrees(std::acos(std::clamp(glm::dot(motion.ground_normal, slope_up), -1.0f, 1.0f)));
    motion.stable_grounded = player.controller.grounded && motion.slope_angle_deg <= tuning.slope_limit_deg;
    if (player.controller.grounded && !motion.stable_grounded) {
        player.controller.grounded = false;
        resolve.grounded = false;
    }

    if (!player.controller.grounded && pre_solve_vertical <= 0.0f) {
        float snap_hit_distance = 0.0f;
        const glm::vec3 snap_up = collision_world.has_planet_surface_collider()
                                      ? collision_world.planet_up_at(player.transform.position)
                                      : glm::vec3(0.0f, 1.0f, 0.0f);
        const glm::vec3 snap_origin = player.transform.position + snap_up * 0.10f;
        if (collision_world.raycast(snap_origin, -snap_up, tuning.ledge_snap_distance, snap_hit_distance)) {
            player.transform.position = snap_origin - snap_up * snap_hit_distance + snap_up * 0.02f;
            player.controller.grounded = true;
            resolve.grounded = true;
        }
    }

    if (player.controller.grounded && !was_grounded && motion.jump_buffer_timer > 0.0f) {
        motion.jump_buffer_timer = 0.0f;
        motion.coyote_timer = 0.0f;
        motion.vertical_velocity = tuning.jump_velocity;
        motion.jump_cut_applied = false;
        player.controller.grounded = false;
        resolve.grounded = false;
        set_locomotion_state(player, PlayerLocomotionState::JumpStart);
    }

    const glm::vec3 actual_velocity = (player.transform.position - start_position) / std::max(0.0001f, dt);
    const glm::vec3 current_up = collision_world.has_planet_surface_collider()
                                     ? collision_world.planet_up_at(player.transform.position)
                                     : glm::vec3(0.0f, 1.0f, 0.0f);
    motion.planar_velocity = actual_velocity - current_up * glm::dot(actual_velocity, current_up);
    motion.move_speed = glm::length(motion.planar_velocity);
    player.controller.velocity = actual_velocity;
    player.controller.velocity =
        motion.planar_velocity + current_up * motion.vertical_velocity;

    if (player.controller.grounded) {
        motion.vertical_velocity = 0.0f;
        if (!was_grounded) {
            motion.just_landed = true;
            motion.landing_impact = std::fabs(pre_solve_vertical);
            set_locomotion_state(
                player,
                motion.landing_impact >= tuning.landing_hard_threshold
                    ? PlayerLocomotionState::LandHard
                    : PlayerLocomotionState::LandSoft);
        } else if (motion.state == PlayerLocomotionState::JumpStart ||
                   motion.state == PlayerLocomotionState::AirborneRise ||
                   motion.state == PlayerLocomotionState::AirborneFall) {
            set_locomotion_state(player, PlayerLocomotionState::LandSoft);
        } else if (motion.state == PlayerLocomotionState::LandSoft && motion.state_timer >= 0.10f) {
            set_locomotion_state(player, PlayerLocomotionState::Idle);
        } else if (motion.state == PlayerLocomotionState::LandHard && motion.state_timer >= tuning.recovery_duration) {
            set_locomotion_state(player, PlayerLocomotionState::Recovery);
        } else if (motion.state == PlayerLocomotionState::Recovery && motion.state_timer >= tuning.recovery_duration) {
            set_locomotion_state(player, PlayerLocomotionState::Idle);
        }
    } else {
        if (motion.vertical_velocity > 0.0f) {
            if (motion.state != PlayerLocomotionState::JumpStart && motion.state_timer > tuning.jump_start_duration) {
                set_locomotion_state(player, PlayerLocomotionState::AirborneRise);
            }
        } else {
            set_locomotion_state(player, PlayerLocomotionState::AirborneFall);
        }
    }

    if (player.controller.grounded) {
        if ((motion.state == PlayerLocomotionState::LandSoft && motion.state_timer < 0.12f) ||
            (motion.state == PlayerLocomotionState::LandHard && motion.state_timer < tuning.recovery_duration) ||
            motion.state == PlayerLocomotionState::Recovery) {
            // Keep one-shot recovery states until their timers expire.
        } else if (!has_move_input && motion.move_speed <= 0.05f) {
            if (std::fabs(motion.turn_delta_deg) > tuning.pivot_threshold_deg && std::fabs(input.look_delta.x) > 0.01f) {
                set_locomotion_state(player, PlayerLocomotionState::TurnInPlace);
            } else {
                set_locomotion_state(player, PlayerLocomotionState::Idle);
            }
        } else if (!has_move_input) {
            set_locomotion_state(player, PlayerLocomotionState::StopMove);
        } else if (motion.move_speed <= 0.35f && motion.state == PlayerLocomotionState::Idle) {
            set_locomotion_state(player, PlayerLocomotionState::StartMove);
        } else if (std::fabs(motion.turn_delta_deg) > tuning.pivot_threshold_deg && motion.move_speed < tuning.walk_speed * 0.45f) {
            set_locomotion_state(player, PlayerLocomotionState::MovingTurn);
        } else if (std::fabs(motion.turn_delta_deg) > tuning.moving_turn_threshold_deg && motion.move_speed >= tuning.walk_speed * 0.45f) {
            set_locomotion_state(player, PlayerLocomotionState::MovingTurn);
        } else {
            set_locomotion_state(player, run_intent ? PlayerLocomotionState::Run : PlayerLocomotionState::Walk);
        }
    }

    const float facing_rad = to_radians(motion.facing_yaw_deg);
    glm::vec3 facing_forward(std::sin(facing_rad), 0.0f, std::cos(facing_rad));
    if (collision_world.has_planet_surface_collider()) {
        // Same tangent basis as camera and movement for consistent orientation.
        glm::vec3 world_ref = glm::vec3(0.0f, 1.0f, 0.0f);
        if (std::abs(glm::dot(current_up, world_ref)) > 0.99f) {
            world_ref = glm::vec3(0.0f, 0.0f, 1.0f);
        }
        const glm::vec3 east = glm::normalize(glm::cross(world_ref, current_up));
        const glm::vec3 north = glm::normalize(glm::cross(current_up, east));
        facing_forward =
            glm::normalize(north * std::cos(facing_rad) + east * std::sin(facing_rad));
    }
    player.transform.rotation = orientation_from_frame(facing_forward, current_up);

    player.procedural.spine_lean = std::clamp(motion.move_speed / std::max(0.1f, tuning.run_speed), 0.0f, 1.0f) * 0.25f;
    player.procedural.turn_bank = std::clamp(motion.turn_delta_deg / 90.0f, -1.0f, 1.0f) * (player.controller.grounded ? 0.18f : 0.08f);
    player.procedural.landing_compression = motion.just_landed
        ? std::clamp(motion.landing_impact / std::max(0.1f, tuning.landing_hard_threshold), 0.0f, 1.0f)
        : std::max(0.0f, player.procedural.landing_compression - dt * 4.0f);
    player.procedural.jump_anticipation = (motion.state == PlayerLocomotionState::JumpStart) ? 1.0f : std::max(0.0f, player.procedural.jump_anticipation - dt * 6.0f);

    debug.had_collision = resolve.had_collision;
    debug.contact_normal = resolve.contact_normal;
    debug.penetration_correction = resolve.total_correction;
    debug.grounded = player.controller.grounded;
    debug.grounding_ray_origin = resolve.ground_ray_origin;
    debug.grounding_ray_hit = resolve.ground_ray_hit;
    debug.overlapped_voxels = resolve.overlapped_voxels;

    update_animation_state(player, input, dt, noclip, was_grounded);
    return debug;
}

void PlayerControllerSystem::update_animation_state(
    PlayerEntity &player,
    const InputState &input,
    float dt,
    bool noclip,
    bool was_grounded) {
    const bool landed_this_frame = !noclip && player.controller.grounded && !was_grounded;
    const PlayerAnimState next_state = map_animation_state(player, input, landed_this_frame);
    const PlayerAnimState prev_state = player.anim_state;
    player.anim_state = next_state;
    player.animation.state = next_state;

    if (prev_state != next_state) {
        player.anim_previous_state = prev_state;
        player.anim_previous_phase = player.anim_phase;
        player.anim_transition_time = 0.0f;
        player.anim_transition_duration = player_anim_crossfade_seconds(next_state);
        player.anim_state_time = 0.0f;
        player.last_anim_event = PlayerAnimEventType::None;
        if (player_animation_definition(next_state).loop_mode == AnimationLoopMode::OneShot) {
            player.anim_phase = 0.0f;
        } else {
            player.anim_phase = std::fmod(player.anim_phase * 0.65f, k_tau);
        }
    } else {
        player.anim_state_time += dt;
    }

    player.anim_phase += player_anim_cycle_rate(next_state) * dt;
    if (player.anim_phase > k_tau) {
        player.anim_phase = std::fmod(player.anim_phase, k_tau);
    }
    if (player.anim_transition_duration > 0.0f) {
        player.anim_transition_time = std::min(player.anim_transition_duration, player.anim_transition_time + dt);
    } else {
        player.anim_transition_time = player.anim_transition_duration;
    }

    float target_blend = player_anim_blend_target(next_state);
    if (player_anim_is_locomotion_cycle(next_state) ||
        next_state == PlayerAnimState::MovingTurn) {
        const float speed_ratio = std::clamp(
            player.locomotion.move_speed / std::max(0.001f, player.locomotion_tuning.run_speed),
            0.0f,
            1.0f);
        target_blend = std::clamp(target_blend * (0.35f + speed_ratio * 0.9f), 0.0f, 1.0f);
    }
    const float blend_step = std::clamp(10.0f * dt, 0.0f, 1.0f);
    player.anim_blend += (target_blend - player.anim_blend) * blend_step;

    const PlayerAnimationStateDefinition &definition = player_animation_definition(next_state);
    const float prev_phase = prev_state != next_state ? 0.0f : std::max(0.0f, player.anim_phase - player_anim_cycle_rate(next_state) * dt);
    player.last_anim_event = PlayerAnimEventType::None;
    const float prev_norm = std::fmod(prev_phase, k_tau) / k_tau;
    const float next_norm = std::fmod(player.anim_phase, k_tau) / k_tau;
    const bool wrapped = next_norm < prev_norm;
    for (const PlayerAnimationEventDefinition &event : definition.events) {
        const float t = std::clamp(event.normalized_time, 0.0f, 1.0f);
        const bool crossed = wrapped ? (t >= prev_norm || t <= next_norm) : (t >= prev_norm && t <= next_norm);
        if (crossed) {
            player.last_anim_event = event.type;
        }
    }

    player.animation.phase = player.anim_phase;
    player.animation.blend = player.anim_blend;
}
