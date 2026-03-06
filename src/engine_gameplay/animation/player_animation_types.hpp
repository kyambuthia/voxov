#pragma once

#include <cstdint>

enum class PlayerAnimState : uint8_t {
    Idle = 0,
    StartMove,
    StopMove,
    LocomotionWalk,
    LocomotionRun,
    JumpTakeoff,
    JumpLoop,
    FallLoop,
    LandSoft,
    LandHard,
    PivotLeft,
    PivotRight,
    TurnInPlaceLeft,
    TurnInPlaceRight,
    MovingTurn,
    Recovery
};

enum class PlayerAnimEventType : uint8_t {
    None = 0,
    Footstep,
    Landing,
    SoundTrigger
};

const char *player_anim_state_name(PlayerAnimState state);
const char *player_anim_event_name(PlayerAnimEventType event_type);
