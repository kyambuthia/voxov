#pragma once

#include "engine_gameplay/animation/player_animation_types.hpp"

#include <cstdint>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

struct TransformComponent {
    glm::vec3 position = glm::vec3(0.0f);
    glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    glm::vec3 scale = glm::vec3(1.0f);
};

struct CharacterController {
    float capsuleRadius = 0.35f;
    float capsuleHeight = 1.8f;
    float walkSpeed = 4.0f;
    float sprintSpeed = 7.2f;
    float crawlSpeed = 2.2f;
    float jumpVelocity = 5.5f;
    float gravity = -19.62f;
    float maxSlopeDeg = 50.0f;
    bool grounded = false;
    glm::vec3 velocity = glm::vec3(0.0f);
};

enum class PlayerMovementState : uint8_t {
    GroundSkating = 0,
    Airborne = 1,
    ManualBalance = 2,
    GrindBalance = 3,
    Bail = 4,
    Recovery = 5
};

enum class PlayerTrickState : uint8_t {
    None = 0,
    Ollie = 1,
    Kickflip = 2,
    ShoveIt = 3,
    Manual = 4,
    Grind = 5,
    Bail = 6,
    Landed = 7
};

struct SkateTuningData {
    float accel = 28.0f;
    float turn_rate = 360.0f;
    float ollie_height = 1.15f;
    float gravity = 24.0f;
    float coyote_time = 0.10f;
    float landing_forgiveness = 0.24f;
    float rail_snap_distance = 0.85f;
    float combo_timeout = 1.75f;
    float max_ground_speed = 8.4f;
    float push_speed_bonus = 1.4f;
    float braking = 34.0f;
    float air_turn_rate = 240.0f;
    float air_control = 7.5f;
    float manual_balance_drift = 0.55f;
    float grind_balance_drift = 0.72f;
    float balance_input_gain = 1.45f;
    float bail_duration = 0.55f;
    float recovery_duration = 0.45f;
    float manual_min_speed = 2.0f;
    float grind_min_speed = 3.0f;
    float hard_landing_speed = 12.5f;
};

struct PlayerMovementStateData {
    PlayerMovementState state = PlayerMovementState::GroundSkating;
    float forward_speed = 0.0f;
    float vertical_velocity = 0.0f;
    float facing_yaw_deg = 180.0f;
    float desired_yaw_deg = 180.0f;
    float state_timer = 0.0f;
    float coyote_timer = 0.0f;
    float landing_timer = 0.0f;
    float balance = 0.0f;
    float balance_impulse = 0.0f;
    float rail_lock_timer = 0.0f;
    glm::vec3 rail_anchor = glm::vec3(0.0f);
    glm::vec3 rail_axis = glm::vec3(0.0f, 0.0f, 1.0f);
    bool just_landed = false;
};

struct PlayerAnimationStateData {
    PlayerAnimState state = PlayerAnimState::Idle;
    float phase = 0.0f;
    float blend = 0.0f;
};

struct PlayerTrickStateData {
    PlayerTrickState state = PlayerTrickState::None;
    float state_timer = 0.0f;
    uint32_t chain_count = 0;
    bool note_placeholder_logic = true;
};

struct PlayerScoreStateData {
    int32_t total_score = 0;
    int32_t combo_score = 0;
    int32_t combo_multiplier = 1;
    uint32_t combo_count = 0;
    float combo_timer = 0.0f;
    bool combo_active = false;
};

struct SkateBoardState {
    glm::vec3 local_offset = glm::vec3(0.0f, 0.14f, 0.0f);
    glm::vec3 half_extents = glm::vec3(0.14f, 0.03f, 0.46f);
    glm::vec3 deck_color = glm::vec3(0.18f, 0.12f, 0.08f);
    float wheel_radius = 0.055f;
    float wheel_track = 0.13f;
    float wheel_base = 0.27f;
};

using SkateMovementState = PlayerMovementState;
using SkateTrickState = PlayerTrickState;
using SkateTuning = SkateTuningData;
using SkateScoreState = PlayerScoreStateData;
using SkateboardState = SkateBoardState;

struct CameraRig {
    float yaw = 180.0f;
    float pitch = -12.0f;
    float distance = 5.0f;
    float minDistance = 1.5f;
    float maxDistance = 8.0f;
    float pivotHeight = 1.5f;
    float sensitivityMouse = 0.11f;
    float sensitivityTouch = 120.0f;
    float pitchMinDeg = -75.0f;
    float pitchMaxDeg = 25.0f;
};

struct PlayerEntity {
    uint32_t network_id = 1;
    TransformComponent transform{};
    CharacterController controller{};
    CameraRig camera_rig{};
    SkateTuningData skate_tuning{};
    PlayerMovementStateData movement{};
    PlayerAnimationStateData animation{};
    PlayerTrickStateData trick{};
    PlayerScoreStateData score{};
    SkateBoardState board{};
    PlayerAnimState anim_state = PlayerAnimState::Idle;
    PlayerAnimState anim_previous_state = PlayerAnimState::Idle;
    float anim_phase = 0.0f;
    float anim_previous_phase = 0.0f;
    float anim_blend = 0.0f;
    float anim_state_time = 0.0f;
    float anim_transition_time = 0.0f;
    float anim_transition_duration = 0.0f;
    float anim_ollie_lock = 0.0f;
    float anim_land_lock = 0.0f;
    PlayerAnimEventType last_anim_event = PlayerAnimEventType::None;
};

struct ReplicatedPlayerMotion {
    uint32_t network_id = 1;
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 velocity = glm::vec3(0.0f);
};
