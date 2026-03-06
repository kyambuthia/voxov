#pragma once

#include <cstdint>

enum class PlayerAnimState : uint8_t {
    Idle = 0,
    Push,
    Cruise,
    TurnLeft,
    TurnRight,
    Ollie,
    Kickflip,
    ShoveIt,
    Manual,
    GrindEnter,
    GrindLoop,
    GrindExit,
    Airborne,
    Land,
    Bail
};

enum class PlayerAnimEventType : uint8_t {
    None = 0,
    BoardContact,
    Landing,
    TrickApex,
    SoundTrigger
};

const char *player_anim_state_name(PlayerAnimState state);
const char *player_anim_event_name(PlayerAnimEventType event_type);
