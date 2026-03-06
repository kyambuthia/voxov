#include "engine_gameplay/animation/player_animation_types.hpp"

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
