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
    float walkSpeed = 3.2f;
    float sprintSpeed = 5.8f;
    float crawlSpeed = 2.2f;
    float jumpVelocity = 5.5f;
    float gravity = -19.62f;
    float maxSlopeDeg = 50.0f;
    bool grounded = false;
    glm::vec3 velocity = glm::vec3(0.0f);
};

enum class PlayerLocomotionState : uint8_t {
    Idle = 0,
    StartMove,
    Walk,
    Run,
    StopMove,
    JumpStart,
    AirborneRise,
    AirborneFall,
    LandSoft,
    LandHard,
    TurnInPlace,
    MovingTurn,
    Slide,
    Recovery
};

struct LocomotionTuningData {
    float walk_speed = 3.2f;
    float run_speed = 5.8f;
    float ground_accel = 28.0f;
    float ground_decel = 32.0f;
    float air_accel = 9.0f;
    float turn_rate = 540.0f;
    float gravity = 24.0f;
    float fall_multiplier = 1.25f;
    float jump_velocity = 6.0f;
    float jump_cut_gravity_multiplier = 2.1f;
    float coyote_time = 0.10f;
    float jump_buffer_time = 0.12f;
    float step_offset = 0.65f;
    float slope_limit_deg = 52.0f;
    float ledge_snap_distance = 0.24f;
    float landing_soft_threshold = 7.5f;
    float landing_hard_threshold = 11.0f;
    float recovery_duration = 0.26f;
    float pivot_threshold_deg = 80.0f;
    float moving_turn_threshold_deg = 30.0f;
    float input_deadzone = 0.14f;
    float run_input_threshold = 0.85f;
    float jump_start_duration = 0.10f;
    float start_move_duration = 0.10f;
    float stop_move_duration = 0.12f;
};

struct PlayerLocomotionStateData {
    PlayerLocomotionState state = PlayerLocomotionState::Idle;
    glm::vec3 planar_velocity = glm::vec3(0.0f);
    glm::vec3 ground_normal = glm::vec3(0.0f, 1.0f, 0.0f);
    float move_speed = 0.0f;
    float vertical_velocity = 0.0f;
    float facing_yaw_deg = 180.0f;
    float desired_yaw_deg = 180.0f;
    float state_timer = 0.0f;
    float coyote_timer = 0.0f;
    float jump_buffer_timer = 0.0f;
    float slope_angle_deg = 0.0f;
    float input_magnitude = 0.0f;
    float move_direction_deg = 0.0f;
    float turn_delta_deg = 0.0f;
    float landing_impact = 0.0f;
    bool stable_grounded = false;
    bool just_landed = false;
    bool jump_cut_applied = false;
};

struct PlayerAnimationStateData {
    PlayerAnimState state = PlayerAnimState::Idle;
    float phase = 0.0f;
    float blend = 0.0f;
};

struct PlayerProceduralStateData {
    float spine_lean = 0.0f;
    float turn_bank = 0.0f;
    float landing_compression = 0.0f;
    float jump_anticipation = 0.0f;
    float upper_body_overlay = 0.0f;
};

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
    float follow_lag = 0.0f;
    float jump_distance_bias = 0.3f;
};

struct PlayerEntity {
    uint32_t network_id = 1;
    TransformComponent transform{};
    CharacterController controller{};
    CameraRig camera_rig{};
    LocomotionTuningData locomotion_tuning{};
    PlayerLocomotionStateData locomotion{};
    PlayerAnimationStateData animation{};
    PlayerProceduralStateData procedural{};
    PlayerAnimState anim_state = PlayerAnimState::Idle;
    PlayerAnimState anim_previous_state = PlayerAnimState::Idle;
    float anim_phase = 0.0f;
    float anim_previous_phase = 0.0f;
    float anim_blend = 0.0f;
    float anim_state_time = 0.0f;
    float anim_transition_time = 0.0f;
    float anim_transition_duration = 0.0f;
    PlayerAnimEventType last_anim_event = PlayerAnimEventType::None;
};

struct ReplicatedPlayerMotion {
    uint32_t network_id = 1;
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 velocity = glm::vec3(0.0f);
};
