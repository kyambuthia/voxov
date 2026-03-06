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

float move_towards(float current, float target, float max_delta) {
    if (current < target) {
        return std::min(current + max_delta, target);
    }
    return std::max(current - max_delta, target);
}

glm::vec3 flat_dir_from_yaw(float yaw_deg) {
    const float yaw_rad = to_radians(yaw_deg);
    return glm::normalize(glm::vec3(std::sin(yaw_rad), 0.0f, std::cos(yaw_rad)));
}

float flat_length(glm::vec3 v) {
    return glm::length(glm::vec2(v.x, v.z));
}

glm::vec3 safe_normalize_flat(glm::vec3 v, glm::vec3 fallback) {
    const float len = flat_length(v);
    if (len <= 0.0001f) {
        return fallback;
    }
    return glm::vec3(v.x / len, 0.0f, v.z / len);
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

float jump_velocity_for_height(const SkateTuningData &tuning) {
    return std::sqrt(std::max(0.01f, 2.0f * tuning.gravity * tuning.ollie_height));
}

struct RailCandidate {
    bool valid = false;
    glm::vec3 anchor = glm::vec3(0.0f);
    glm::vec3 axis = glm::vec3(0.0f, 0.0f, 1.0f);
    float score = std::numeric_limits<float>::max();
};

bool top_open(const VoxelCollisionWorld &collision_world, int x, int y, int z) {
    return !collision_world.is_solid_voxel(x, y + 1, z);
}

void consider_rail_candidate(
    RailCandidate &best,
    const VoxelCollisionWorld &collision_world,
    glm::vec3 feet_position,
    glm::vec3 preferred_axis,
    int x,
    int z,
    bool along_x) {
    const int y = top_solid_y(collision_world, x, z);
    if (y < 0 || !top_open(collision_world, x, y, z)) {
        return;
    }

    const int nx0 = along_x ? (x - 1) : x;
    const int nz0 = along_x ? z : (z - 1);
    const int nx1 = along_x ? (x + 1) : x;
    const int nz1 = along_x ? z : (z + 1);
    const int sx0 = along_x ? x : (x - 1);
    const int sz0 = along_x ? (z - 1) : z;
    const int sx1 = along_x ? x : (x + 1);
    const int sz1 = along_x ? (z + 1) : z;

    const int top0 = top_solid_y(collision_world, nx0, nz0);
    const int top1 = top_solid_y(collision_world, nx1, nz1);
    const bool connected = (top0 == y && top_open(collision_world, nx0, y, nz0)) ||
                           (top1 == y && top_open(collision_world, nx1, y, nz1));
    if (!connected) {
        return;
    }

    const bool side_a_open = top_solid_y(collision_world, sx0, sz0) < y;
    const bool side_b_open = top_solid_y(collision_world, sx1, sz1) < y;
    if (!side_a_open && !side_b_open) {
        return;
    }

    RailCandidate candidate{};
    candidate.valid = true;
    candidate.axis = along_x ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 0.0f, 1.0f);
    if (glm::dot(candidate.axis, preferred_axis) < 0.0f) {
        candidate.axis *= -1.0f;
    }
    candidate.anchor = glm::vec3(
        along_x ? feet_position.x : (static_cast<float>(x) + 0.5f),
        static_cast<float>(y) + 1.02f,
        along_x ? (static_cast<float>(z) + 0.5f) : feet_position.z);

    const glm::vec3 snap_point(
        along_x ? feet_position.x : candidate.anchor.x,
        candidate.anchor.y,
        along_x ? candidate.anchor.z : feet_position.z);
    const float lateral_dist = glm::length(glm::vec2(snap_point.x - feet_position.x, snap_point.z - feet_position.z));
    const float vertical_dist = std::fabs(candidate.anchor.y - feet_position.y);
    const float alignment_bonus = 1.0f - std::clamp(std::fabs(glm::dot(glm::normalize(candidate.axis), glm::normalize(preferred_axis))), 0.0f, 1.0f);
    candidate.score = lateral_dist + vertical_dist * 1.6f + alignment_bonus * 0.2f;

    if (candidate.score < best.score) {
        best = candidate;
    }
}

RailCandidate find_best_rail_candidate(
    const VoxelCollisionWorld &collision_world,
    glm::vec3 feet_position,
    glm::vec3 preferred_axis,
    float snap_distance) {
    RailCandidate best{};
    const int min_x = std::max(1, static_cast<int>(std::floor(feet_position.x - snap_distance - 1.0f)));
    const int max_x = std::min(VoxelChunk::CHUNK_X - 2, static_cast<int>(std::floor(feet_position.x + snap_distance + 1.0f)));
    const int min_z = std::max(1, static_cast<int>(std::floor(feet_position.z - snap_distance - 1.0f)));
    const int max_z = std::min(VoxelChunk::CHUNK_Z - 2, static_cast<int>(std::floor(feet_position.z + snap_distance + 1.0f)));
    const glm::vec3 fallback_axis = safe_normalize_flat(preferred_axis, glm::vec3(0.0f, 0.0f, 1.0f));

    for (int z = min_z; z <= max_z; ++z) {
        for (int x = min_x; x <= max_x; ++x) {
            consider_rail_candidate(best, collision_world, feet_position, fallback_axis, x, z, true);
            consider_rail_candidate(best, collision_world, feet_position, fallback_axis, x, z, false);
        }
    }

    if (!best.valid) {
        return best;
    }

    const float lateral_dist = glm::length(glm::vec2(best.anchor.x - feet_position.x, best.anchor.z - feet_position.z));
    const float vertical_dist = std::fabs(best.anchor.y - feet_position.y);
    if (lateral_dist > snap_distance || vertical_dist > snap_distance * 0.7f) {
        return RailCandidate{};
    }
    return best;
}

void set_movement_state(PlayerEntity &player, PlayerMovementState state, float timer = 0.0f) {
    if (player.movement.state != state) {
        player.movement.state = state;
        player.movement.state_timer = timer;
    } else {
        player.movement.state_timer = std::max(player.movement.state_timer, timer);
    }
}

void set_trick_state(PlayerEntity &player, PlayerTrickState state) {
    if (player.trick.state != state) {
        player.trick.state = state;
        player.trick.state_timer = 0.0f;
    }
}

void add_combo_score(PlayerEntity &player, int base_points) {
    player.score.combo_active = true;
    player.score.combo_timer = player.skate_tuning.combo_timeout;
    player.score.combo_score += base_points * std::max(1, player.score.combo_multiplier);
}

void begin_combo_trick(PlayerEntity &player, PlayerTrickState trick_state, int base_points) {
    if (player.trick.state != trick_state) {
        player.trick.chain_count += 1;
        player.score.combo_count += 1;
        player.score.combo_multiplier = std::max(1, static_cast<int32_t>(player.score.combo_count));
        set_trick_state(player, trick_state);
        add_combo_score(player, base_points);
    }
}

void reset_combo(PlayerEntity &player) {
    player.score.combo_score = 0;
    player.score.combo_multiplier = 1;
    player.score.combo_count = 0;
    player.score.combo_timer = 0.0f;
    player.score.combo_active = false;
}

void bank_combo_if_ready(PlayerEntity &player) {
    if (player.score.combo_active &&
        player.score.combo_timer <= 0.0f &&
        player.movement.state != PlayerMovementState::Airborne &&
        player.movement.state != PlayerMovementState::ManualBalance &&
        player.movement.state != PlayerMovementState::GrindBalance) {
        player.score.total_score += player.score.combo_score;
        reset_combo(player);
        if (player.trick.state == PlayerTrickState::Landed) {
            set_trick_state(player, PlayerTrickState::None);
            player.trick.chain_count = 0;
        }
    }
}

void enter_bail(PlayerEntity &player) {
    set_movement_state(player, PlayerMovementState::Bail, player.skate_tuning.bail_duration);
    player.movement.balance = 0.0f;
    player.movement.balance_impulse = 0.0f;
    set_trick_state(player, PlayerTrickState::Bail);
    player.trick.chain_count = 0;
    reset_combo(player);
}

CapsuleResolveResult simulate_capsule(
    PlayerEntity &player,
    const VoxelCollisionWorld &collision_world,
    glm::vec3 desired_flat_velocity,
    float dt) {
    glm::vec3 next_pos = player.transform.position + desired_flat_velocity * dt;
    next_pos.y += player.movement.vertical_velocity * dt;

    CapsuleResolveResult resolve = collision_world.resolve_capsule(
        next_pos,
        player.controller.capsuleRadius,
        player.controller.capsuleHeight,
        0.02f,
        8,
        1.2f);

    if (player.controller.grounded && flat_length(desired_flat_velocity) > 0.001f && resolve.had_collision) {
        const float step_height = 0.65f;
        glm::vec3 step_test = player.transform.position + desired_flat_velocity * dt;
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

    return resolve;
}

PlayerAnimState map_animation_state(const PlayerEntity &player, const InputState &input, bool noclip, bool landed_this_frame) {
    const float horizontal_speed = player.movement.forward_speed;
    const bool moving = horizontal_speed > 0.2f || glm::length(input.move) > 0.12f;

    if (noclip) {
        return moving ? PlayerAnimState::Cruise : PlayerAnimState::Idle;
    }

    if (player.anim_land_lock > 0.0f && player.controller.grounded) {
        return PlayerAnimState::Land;
    }
    if (player.anim_ollie_lock > 0.0f) {
        if (player.trick.state == PlayerTrickState::Kickflip) {
            return PlayerAnimState::Kickflip;
        }
        if (player.trick.state == PlayerTrickState::ShoveIt) {
            return PlayerAnimState::ShoveIt;
        }
        return PlayerAnimState::Ollie;
    }

    switch (player.movement.state) {
    case PlayerMovementState::Bail:
        return PlayerAnimState::Bail;
    case PlayerMovementState::Recovery:
        return landed_this_frame ? PlayerAnimState::Land : PlayerAnimState::Idle;
    case PlayerMovementState::GrindBalance:
        return player.movement.state_timer > 0.08f ? PlayerAnimState::GrindEnter : PlayerAnimState::GrindLoop;
    case PlayerMovementState::ManualBalance:
        return PlayerAnimState::Manual;
    case PlayerMovementState::Airborne:
        if (player.trick.state == PlayerTrickState::Kickflip) {
            return PlayerAnimState::Kickflip;
        }
        if (player.trick.state == PlayerTrickState::ShoveIt) {
            return PlayerAnimState::ShoveIt;
        }
        return player.trick.state == PlayerTrickState::Ollie ? PlayerAnimState::Ollie : PlayerAnimState::Airborne;
    case PlayerMovementState::GroundSkating:
    default:
        if (landed_this_frame) {
            return PlayerAnimState::Land;
        }
        if (moving) {
            const float turn_input = input.move.x;
            if (input.sprint_held || horizontal_speed > player.skate_tuning.max_ground_speed * 0.82f) {
                return PlayerAnimState::Push;
            }
            if (turn_input < -0.4f) {
                return PlayerAnimState::TurnLeft;
            }
            if (turn_input > 0.4f) {
                return PlayerAnimState::TurnRight;
            }
            return PlayerAnimState::Cruise;
        }
        return PlayerAnimState::Idle;
    }
}
}

const char *player_movement_state_name(PlayerMovementState state) {
    switch (state) {
    case PlayerMovementState::GroundSkating:
        return "GROUND";
    case PlayerMovementState::Airborne:
        return "AIR";
    case PlayerMovementState::ManualBalance:
        return "MANUAL";
    case PlayerMovementState::GrindBalance:
        return "GRIND";
    case PlayerMovementState::Bail:
        return "BAIL";
    case PlayerMovementState::Recovery:
        return "RECOVER";
    default:
        return "UNK";
    }
}

const char *player_trick_state_name(PlayerTrickState state) {
    switch (state) {
    case PlayerTrickState::None:
        return "NONE";
    case PlayerTrickState::Ollie:
        return "OLLIE";
    case PlayerTrickState::Kickflip:
        return "KICKFLIP";
    case PlayerTrickState::ShoveIt:
        return "SHOVE_IT";
    case PlayerTrickState::Manual:
        return "MANUAL";
    case PlayerTrickState::Grind:
        return "GRIND";
    case PlayerTrickState::Bail:
        return "BAIL";
    case PlayerTrickState::Landed:
        return "LANDED";
    default:
        return "UNK";
    }
}

const char *player_trick_note() {
    return "placeholder flip/spin buckets; no stance-specific parser yet";
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
    player.transform.position.y = collision_world.find_spawn_height(
        glm::vec2(player.transform.position.x, player.transform.position.z),
        player.controller.capsuleRadius,
        player.controller.capsuleHeight);
    player.transform.position.y += 0.05f;
    player.movement.facing_yaw_deg = player.camera_rig.yaw;
    player.movement.desired_yaw_deg = player.camera_rig.yaw;
    player.transform.rotation = glm::angleAxis(to_radians(player.movement.facing_yaw_deg), glm::vec3(0.0f, 1.0f, 0.0f));
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
    const SkateTuningData &tuning = player.skate_tuning;
    const MovementDebug movement_debug = compute_movement_vectors(player.camera_rig.yaw, input.move);
    glm::vec3 desired_move = movement_debug.desired;
    const bool has_move_input = glm::length(desired_move) > 0.08f;
    if (has_move_input) {
        desired_move = glm::normalize(desired_move);
        player.movement.desired_yaw_deg = to_degrees(std::atan2(desired_move.x, desired_move.z));
    }

    player.movement.just_landed = false;
    player.movement.state_timer = std::max(0.0f, player.movement.state_timer - dt);
    player.movement.rail_lock_timer = std::max(0.0f, player.movement.rail_lock_timer - dt);
    player.trick.state_timer += dt;

    if (player.score.combo_active) {
        const bool chain_live =
            player.movement.state == PlayerMovementState::Airborne ||
            player.movement.state == PlayerMovementState::ManualBalance ||
            player.movement.state == PlayerMovementState::GrindBalance;
        if (chain_live) {
            player.score.combo_timer = tuning.combo_timeout;
        } else {
            player.score.combo_timer = std::max(0.0f, player.score.combo_timer - dt);
        }
    }

    const bool was_grounded = player.controller.grounded;
    if (was_grounded) {
        player.movement.coyote_timer = tuning.coyote_time;
    } else {
        player.movement.coyote_timer = std::max(0.0f, player.movement.coyote_timer - dt);
    }

    if (noclip) {
        const float noclip_speed = tuning.max_ground_speed + (input.sprint_held ? tuning.push_speed_bonus : 0.0f);
        player.transform.position += (has_move_input ? desired_move : glm::vec3(0.0f)) * noclip_speed * dt;
        if (input.jump_held) {
            player.transform.position.y += noclip_speed * dt;
        }
        player.controller.grounded = false;
        player.controller.velocity = glm::vec3(0.0f);
        set_movement_state(player, PlayerMovementState::Airborne);
        update_animation_state(player, input, dt, noclip, was_grounded);
        bank_combo_if_ready(player);
        return debug;
    }

    const float push_target_speed =
        std::max(0.0f, input.move.y) * (tuning.max_ground_speed + (input.sprint_held ? tuning.push_speed_bonus : 0.0f));
    const float facing_turn_rate =
        (player.movement.state == PlayerMovementState::Airborne) ? tuning.air_turn_rate : tuning.turn_rate;

    if (player.movement.state != PlayerMovementState::GrindBalance && has_move_input) {
        player.movement.facing_yaw_deg = rotate_towards_deg(
            player.movement.facing_yaw_deg,
            player.movement.desired_yaw_deg,
            facing_turn_rate * dt);
    }

    if (player.movement.state == PlayerMovementState::Bail) {
        player.movement.forward_speed = move_towards(player.movement.forward_speed, 0.0f, tuning.braking * 0.6f * dt);
        player.movement.vertical_velocity -= tuning.gravity * dt;
    } else if (player.movement.state == PlayerMovementState::Recovery) {
        player.movement.forward_speed = move_towards(player.movement.forward_speed, 0.0f, tuning.braking * dt);
        if (!player.controller.grounded) {
            player.movement.vertical_velocity -= tuning.gravity * dt;
        }
    } else if (player.movement.state == PlayerMovementState::GrindBalance) {
        player.movement.forward_speed = std::max(player.movement.forward_speed, tuning.grind_min_speed);
        player.movement.balance +=
            (std::sin((player.trick.state_timer + static_cast<float>(player.network_id)) * 2.4f) * tuning.grind_balance_drift -
             input.move.x * tuning.balance_input_gain) *
            dt;
        player.movement.balance = std::clamp(player.movement.balance, -1.4f, 1.4f);
        if (std::fabs(player.movement.balance) >= 1.0f) {
            enter_bail(player);
        }
    } else {
        if (player.movement.state == PlayerMovementState::Airborne) {
            const float air_target = std::max(player.movement.forward_speed, push_target_speed * 0.9f);
            player.movement.forward_speed = move_towards(player.movement.forward_speed, air_target, tuning.air_control * dt);
            player.movement.vertical_velocity -= tuning.gravity * dt;
        } else {
            float target_speed = push_target_speed;
            float speed_rate = (target_speed > player.movement.forward_speed) ? tuning.accel : tuning.braking;
            if (input.move.y < -0.05f) {
                target_speed = 0.0f;
                speed_rate = tuning.braking * (1.0f + std::fabs(input.move.y));
            }
            player.movement.forward_speed = move_towards(player.movement.forward_speed, target_speed, speed_rate * dt);
        }

        const bool can_manual =
            input.crouch_held &&
            player.controller.grounded &&
            player.movement.forward_speed >= tuning.manual_min_speed &&
            player.movement.state != PlayerMovementState::Airborne;
        if (can_manual) {
            if (player.movement.state != PlayerMovementState::ManualBalance) {
                set_movement_state(player, PlayerMovementState::ManualBalance);
                player.movement.balance = 0.0f;
                begin_combo_trick(player, PlayerTrickState::Manual, 150);
            }
            player.movement.balance +=
                (std::sin((player.trick.state_timer + static_cast<float>(player.network_id)) * 1.8f) * tuning.manual_balance_drift -
                 input.move.x * tuning.balance_input_gain) *
                dt;
            player.movement.balance = std::clamp(player.movement.balance, -1.4f, 1.4f);
            if (std::fabs(player.movement.balance) >= 1.0f) {
                enter_bail(player);
            }
        } else if (player.movement.state == PlayerMovementState::ManualBalance) {
            set_movement_state(player, PlayerMovementState::GroundSkating);
            player.movement.balance = 0.0f;
            set_trick_state(player, PlayerTrickState::Landed);
        } else if (player.movement.state != PlayerMovementState::Airborne) {
            set_movement_state(player, PlayerMovementState::GroundSkating);
        }
    }

    const bool wants_jump = input.jump_pressed;
    const bool can_ground_jump =
        player.movement.state != PlayerMovementState::Bail &&
        player.movement.state != PlayerMovementState::Recovery &&
        (player.controller.grounded || player.movement.coyote_timer > 0.0f);
    if (wants_jump && can_ground_jump) {
        set_movement_state(player, PlayerMovementState::Airborne);
        player.movement.vertical_velocity = jump_velocity_for_height(tuning);
        player.controller.grounded = false;
        player.movement.coyote_timer = 0.0f;
        player.anim_ollie_lock = 0.16f;
        begin_combo_trick(player, PlayerTrickState::Ollie, 100);
    } else if (wants_jump && player.movement.state == PlayerMovementState::GrindBalance) {
        set_movement_state(player, PlayerMovementState::Airborne);
        player.movement.vertical_velocity = jump_velocity_for_height(tuning) * 0.95f;
        player.movement.rail_lock_timer = 0.0f;
        player.anim_ollie_lock = 0.16f;
        begin_combo_trick(player, PlayerTrickState::Ollie, 90);
    }

    if (player.movement.state == PlayerMovementState::GrindBalance) {
        player.transform.position += player.movement.rail_axis * player.movement.forward_speed * dt;
        player.transform.position.y = player.movement.rail_anchor.y;
        const RailCandidate rail = find_best_rail_candidate(
            collision_world,
            player.transform.position,
            player.movement.rail_axis,
            tuning.rail_snap_distance * 1.2f);
        if (rail.valid) {
            player.movement.rail_axis = rail.axis;
            player.movement.rail_anchor = rail.anchor;
            if (std::fabs(rail.axis.x) > 0.5f) {
                player.transform.position.z = rail.anchor.z;
            } else {
                player.transform.position.x = rail.anchor.x;
            }
            player.transform.position.y = rail.anchor.y;
        } else if (player.movement.rail_lock_timer <= 0.0f) {
            set_movement_state(player, PlayerMovementState::Airborne);
        }

        player.controller.velocity = player.movement.rail_axis * player.movement.forward_speed;
        player.controller.grounded = false;
        player.transform.rotation = glm::angleAxis(
            std::atan2(player.movement.rail_axis.x, player.movement.rail_axis.z),
            glm::vec3(0.0f, 1.0f, 0.0f));
        update_animation_state(player, input, dt, noclip, was_grounded);
        bank_combo_if_ready(player);
        return debug;
    }

    const glm::vec3 flat_forward = flat_dir_from_yaw(player.movement.facing_yaw_deg);
    const glm::vec3 desired_flat_velocity = flat_forward * player.movement.forward_speed;
    const float pre_solve_vertical_velocity = player.movement.vertical_velocity;
    const glm::vec3 start_position = player.transform.position;
    CapsuleResolveResult resolve = simulate_capsule(player, collision_world, desired_flat_velocity, dt);

    player.transform.position = resolve.position;
    player.controller.grounded = resolve.grounded;

    if (!resolve.grounded &&
        player.movement.state != PlayerMovementState::Airborne &&
        player.movement.state != PlayerMovementState::Bail &&
        player.movement.state != PlayerMovementState::Recovery) {
        set_movement_state(player, PlayerMovementState::Airborne);
    }

    if (!resolve.grounded && pre_solve_vertical_velocity <= 0.0f) {
        float snap_hit_distance = 0.0f;
        const glm::vec3 snap_origin = player.transform.position + glm::vec3(0.0f, 0.10f, 0.0f);
        if (collision_world.raycast(
                snap_origin,
                glm::vec3(0.0f, -1.0f, 0.0f),
                std::max(0.12f, tuning.landing_forgiveness),
                snap_hit_distance)) {
            player.transform.position.y = snap_origin.y - snap_hit_distance + 0.02f;
            player.controller.grounded = true;
            resolve.grounded = true;
            resolve.ground_ray_origin = snap_origin;
            resolve.ground_ray_hit = player.transform.position;
        }
    }

    if (!player.controller.grounded &&
        player.movement.state == PlayerMovementState::Airborne &&
        input.crouch_held &&
        player.movement.forward_speed >= tuning.grind_min_speed) {
        const RailCandidate rail = find_best_rail_candidate(
            collision_world,
            player.transform.position,
            has_move_input ? desired_move : desired_flat_velocity,
            tuning.rail_snap_distance);
        if (rail.valid) {
            set_movement_state(player, PlayerMovementState::GrindBalance, 0.18f);
            player.movement.rail_axis = rail.axis;
            player.movement.rail_anchor = rail.anchor;
            player.movement.rail_lock_timer = 0.18f;
            player.movement.balance = 0.0f;
            player.transform.position.y = rail.anchor.y;
            if (std::fabs(rail.axis.x) > 0.5f) {
                player.transform.position.z = rail.anchor.z;
            } else {
                player.transform.position.x = rail.anchor.x;
            }
            begin_combo_trick(player, PlayerTrickState::Grind, 250);
            player.controller.velocity = rail.axis * player.movement.forward_speed;
            player.controller.grounded = false;
            player.transform.rotation = glm::angleAxis(
                std::atan2(rail.axis.x, rail.axis.z),
                glm::vec3(0.0f, 1.0f, 0.0f));
            update_animation_state(player, input, dt, noclip, was_grounded);
            bank_combo_if_ready(player);
            return debug;
        }
    }

    const glm::vec3 actual_velocity = (player.transform.position - start_position) / std::max(0.0001f, dt);
    player.controller.velocity = actual_velocity;
    player.controller.velocity.y = player.movement.vertical_velocity;

    if (player.controller.grounded) {
        player.movement.forward_speed = std::max(0.0f, glm::dot(glm::vec3(actual_velocity.x, 0.0f, actual_velocity.z), flat_forward));
        if (pre_solve_vertical_velocity < -tuning.hard_landing_speed) {
            enter_bail(player);
        } else if (!was_grounded ||
                   player.movement.state == PlayerMovementState::Airborne ||
                   player.trick.state == PlayerTrickState::Ollie ||
                   player.trick.state == PlayerTrickState::Grind ||
                   player.trick.state == PlayerTrickState::Kickflip ||
                   player.trick.state == PlayerTrickState::ShoveIt) {
            player.movement.just_landed = true;
            player.movement.balance = 0.0f;
            player.movement.vertical_velocity = 0.0f;
            player.anim_land_lock = 0.14f;
            if (player.movement.state != PlayerMovementState::Bail && player.movement.state != PlayerMovementState::Recovery) {
                set_movement_state(
                    player,
                    input.crouch_held && player.movement.forward_speed >= tuning.manual_min_speed
                        ? PlayerMovementState::ManualBalance
                        : PlayerMovementState::GroundSkating);
            }
            if (player.trick.state != PlayerTrickState::Bail) {
                set_trick_state(player, PlayerTrickState::Landed);
                add_combo_score(player, 75);
            }
        } else {
            player.movement.vertical_velocity = 0.0f;
            if (player.movement.state == PlayerMovementState::Airborne) {
                set_movement_state(player, PlayerMovementState::GroundSkating);
            }
        }
    } else {
        if (player.movement.state != PlayerMovementState::Bail && player.movement.state != PlayerMovementState::Recovery) {
            set_movement_state(player, PlayerMovementState::Airborne);
        }
        player.controller.velocity.y = player.movement.vertical_velocity;
    }

    if (player.movement.state == PlayerMovementState::Bail && player.controller.grounded && player.movement.state_timer <= 0.0f) {
        set_movement_state(player, PlayerMovementState::Recovery, tuning.recovery_duration);
        player.movement.vertical_velocity = 0.0f;
    } else if (player.movement.state == PlayerMovementState::Recovery && player.movement.state_timer <= 0.0f) {
        set_movement_state(player, player.controller.grounded ? PlayerMovementState::GroundSkating : PlayerMovementState::Airborne);
    }

    player.transform.rotation = glm::angleAxis(to_radians(player.movement.facing_yaw_deg), glm::vec3(0.0f, 1.0f, 0.0f));

    debug.had_collision = resolve.had_collision;
    debug.contact_normal = resolve.contact_normal;
    debug.penetration_correction = resolve.total_correction;
    debug.grounded = player.controller.grounded;
    debug.grounding_ray_origin = resolve.ground_ray_origin;
    debug.grounding_ray_hit = resolve.ground_ray_hit;
    debug.overlapped_voxels = resolve.overlapped_voxels;

    update_animation_state(player, input, dt, noclip, was_grounded);
    bank_combo_if_ready(player);
    return debug;
}

void PlayerControllerSystem::update_animation_state(
    PlayerEntity &player,
    const InputState &input,
    float dt,
    bool noclip,
    bool was_grounded) {
    const float horizontal_speed = player.movement.forward_speed;
    const float speed_ratio = std::clamp(horizontal_speed / std::max(0.001f, player.skate_tuning.max_ground_speed), 0.0f, 1.2f);
    const bool landed_this_frame = !noclip && player.controller.grounded && !was_grounded;

    player.anim_land_lock = std::max(0.0f, player.anim_land_lock - dt);
    player.anim_ollie_lock = std::max(0.0f, player.anim_ollie_lock - dt);

    const PlayerAnimState next_state = map_animation_state(player, input, noclip, landed_this_frame);
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
    if (next_state == PlayerAnimState::Cruise || next_state == PlayerAnimState::Push) {
        target_blend = std::clamp(target_blend * (0.45f + speed_ratio * 0.8f), 0.0f, 1.0f);
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
