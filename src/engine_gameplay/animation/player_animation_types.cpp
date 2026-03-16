#include "engine_gameplay/animation/player_animation_types.hpp"

namespace {
float wrap_anim_degrees(float deg) {
    while (deg > 180.0f) {
        deg -= 360.0f;
    }
    while (deg < -180.0f) {
        deg += 360.0f;
    }
    return deg;
}
}

const char *player_anim_state_name(PlayerAnimState state) {
    switch (state) {
    case PlayerAnimState::Idle:
        return "IDLE";
    case PlayerAnimState::StartMove:
        return "START_MOVE";
    case PlayerAnimState::StopMove:
        return "STOP_MOVE";
    case PlayerAnimState::LocomotionWalk:
        return "LOCOMOTION_WALK";
    case PlayerAnimState::LocomotionRun:
        return "LOCOMOTION_RUN";
    case PlayerAnimState::LocomotionWalkForwardRight:
        return "LOCOMOTION_WALK_FORWARD_RIGHT";
    case PlayerAnimState::LocomotionWalkRight:
        return "LOCOMOTION_WALK_RIGHT";
    case PlayerAnimState::LocomotionWalkBackwardRight:
        return "LOCOMOTION_WALK_BACKWARD_RIGHT";
    case PlayerAnimState::LocomotionWalkBackward:
        return "LOCOMOTION_WALK_BACKWARD";
    case PlayerAnimState::LocomotionWalkBackwardLeft:
        return "LOCOMOTION_WALK_BACKWARD_LEFT";
    case PlayerAnimState::LocomotionWalkLeft:
        return "LOCOMOTION_WALK_LEFT";
    case PlayerAnimState::LocomotionWalkForwardLeft:
        return "LOCOMOTION_WALK_FORWARD_LEFT";
    case PlayerAnimState::LocomotionRunForwardRight:
        return "LOCOMOTION_RUN_FORWARD_RIGHT";
    case PlayerAnimState::LocomotionRunRight:
        return "LOCOMOTION_RUN_RIGHT";
    case PlayerAnimState::LocomotionRunBackwardRight:
        return "LOCOMOTION_RUN_BACKWARD_RIGHT";
    case PlayerAnimState::LocomotionRunBackward:
        return "LOCOMOTION_RUN_BACKWARD";
    case PlayerAnimState::LocomotionRunBackwardLeft:
        return "LOCOMOTION_RUN_BACKWARD_LEFT";
    case PlayerAnimState::LocomotionRunLeft:
        return "LOCOMOTION_RUN_LEFT";
    case PlayerAnimState::LocomotionRunForwardLeft:
        return "LOCOMOTION_RUN_FORWARD_LEFT";
    case PlayerAnimState::JumpTakeoff:
        return "JUMP_TAKEOFF";
    case PlayerAnimState::JumpLoop:
        return "JUMP_LOOP";
    case PlayerAnimState::FallLoop:
        return "FALL_LOOP";
    case PlayerAnimState::LandSoft:
        return "LAND_SOFT";
    case PlayerAnimState::LandHard:
        return "LAND_HARD";
    case PlayerAnimState::PivotLeft:
        return "PIVOT_LEFT";
    case PlayerAnimState::PivotRight:
        return "PIVOT_RIGHT";
    case PlayerAnimState::TurnInPlaceLeft:
        return "TURN_IN_PLACE_LEFT";
    case PlayerAnimState::TurnInPlaceRight:
        return "TURN_IN_PLACE_RIGHT";
    case PlayerAnimState::MovingTurn:
        return "MOVING_TURN";
    case PlayerAnimState::Recovery:
        return "RECOVERY";
    default:
        return "UNKNOWN";
    }
}

bool player_anim_is_walk_cycle(PlayerAnimState state) {
    switch (state) {
    case PlayerAnimState::LocomotionWalk:
    case PlayerAnimState::LocomotionWalkForwardRight:
    case PlayerAnimState::LocomotionWalkRight:
    case PlayerAnimState::LocomotionWalkBackwardRight:
    case PlayerAnimState::LocomotionWalkBackward:
    case PlayerAnimState::LocomotionWalkBackwardLeft:
    case PlayerAnimState::LocomotionWalkLeft:
    case PlayerAnimState::LocomotionWalkForwardLeft:
        return true;
    default:
        return false;
    }
}

bool player_anim_is_run_cycle(PlayerAnimState state) {
    switch (state) {
    case PlayerAnimState::LocomotionRun:
    case PlayerAnimState::LocomotionRunForwardRight:
    case PlayerAnimState::LocomotionRunRight:
    case PlayerAnimState::LocomotionRunBackwardRight:
    case PlayerAnimState::LocomotionRunBackward:
    case PlayerAnimState::LocomotionRunBackwardLeft:
    case PlayerAnimState::LocomotionRunLeft:
    case PlayerAnimState::LocomotionRunForwardLeft:
        return true;
    default:
        return false;
    }
}

bool player_anim_is_locomotion_cycle(PlayerAnimState state) {
    return player_anim_is_walk_cycle(state) || player_anim_is_run_cycle(state);
}

PlayerAnimState player_anim_directional_locomotion_state(bool run, float direction_deg) {
    const float angle = wrap_anim_degrees(direction_deg);
    if (angle >= -22.5f && angle < 22.5f) {
        return run ? PlayerAnimState::LocomotionRun
                   : PlayerAnimState::LocomotionWalk;
    }
    if (angle >= 22.5f && angle < 67.5f) {
        return run ? PlayerAnimState::LocomotionRunForwardRight
                   : PlayerAnimState::LocomotionWalkForwardRight;
    }
    if (angle >= 67.5f && angle < 112.5f) {
        return run ? PlayerAnimState::LocomotionRunRight
                   : PlayerAnimState::LocomotionWalkRight;
    }
    if (angle >= 112.5f && angle < 157.5f) {
        return run ? PlayerAnimState::LocomotionRunBackwardRight
                   : PlayerAnimState::LocomotionWalkBackwardRight;
    }
    if (angle >= 157.5f || angle < -157.5f) {
        return run ? PlayerAnimState::LocomotionRunBackward
                   : PlayerAnimState::LocomotionWalkBackward;
    }
    if (angle >= -157.5f && angle < -112.5f) {
        return run ? PlayerAnimState::LocomotionRunBackwardLeft
                   : PlayerAnimState::LocomotionWalkBackwardLeft;
    }
    if (angle >= -112.5f && angle < -67.5f) {
        return run ? PlayerAnimState::LocomotionRunLeft
                   : PlayerAnimState::LocomotionWalkLeft;
    }
    return run ? PlayerAnimState::LocomotionRunForwardLeft
               : PlayerAnimState::LocomotionWalkForwardLeft;
}

const char *player_anim_event_name(PlayerAnimEventType event_type) {
    switch (event_type) {
    case PlayerAnimEventType::None:
        return "NONE";
    case PlayerAnimEventType::Footstep:
        return "FOOTSTEP";
    case PlayerAnimEventType::Landing:
        return "LANDING";
    case PlayerAnimEventType::SoundTrigger:
        return "SOUND_TRIGGER";
    default:
        return "UNKNOWN";
    }
}
