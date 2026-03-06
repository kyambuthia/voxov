#include "engine_gameplay/animation/player_animation_types.hpp"

const char *player_anim_state_name(PlayerAnimState state) {
    switch (state) {
    case PlayerAnimState::Idle:
        return "IDLE";
    case PlayerAnimState::Push:
        return "PUSH";
    case PlayerAnimState::Cruise:
        return "CRUISE";
    case PlayerAnimState::TurnLeft:
        return "TURN_LEFT";
    case PlayerAnimState::TurnRight:
        return "TURN_RIGHT";
    case PlayerAnimState::Ollie:
        return "OLLIE";
    case PlayerAnimState::Kickflip:
        return "KICKFLIP";
    case PlayerAnimState::ShoveIt:
        return "SHOVE_IT";
    case PlayerAnimState::Manual:
        return "MANUAL";
    case PlayerAnimState::GrindEnter:
        return "GRIND_ENTER";
    case PlayerAnimState::GrindLoop:
        return "GRIND_LOOP";
    case PlayerAnimState::GrindExit:
        return "GRIND_EXIT";
    case PlayerAnimState::Airborne:
        return "AIRBORNE";
    case PlayerAnimState::Land:
        return "LAND";
    case PlayerAnimState::Bail:
        return "BAIL";
    default:
        return "UNKNOWN";
    }
}

const char *player_anim_event_name(PlayerAnimEventType event_type) {
    switch (event_type) {
    case PlayerAnimEventType::None:
        return "NONE";
    case PlayerAnimEventType::BoardContact:
        return "BOARD_CONTACT";
    case PlayerAnimEventType::Landing:
        return "LANDING";
    case PlayerAnimEventType::TrickApex:
        return "TRICK_APEX";
    case PlayerAnimEventType::SoundTrigger:
        return "SOUND_TRIGGER";
    default:
        return "UNKNOWN";
    }
}
