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
    Recovery,
    LocomotionWalkForwardRight,
    LocomotionWalkRight,
    LocomotionWalkBackwardRight,
    LocomotionWalkBackward,
    LocomotionWalkBackwardLeft,
    LocomotionWalkLeft,
    LocomotionWalkForwardLeft,
    LocomotionRunForwardRight,
    LocomotionRunRight,
    LocomotionRunBackwardRight,
    LocomotionRunBackward,
    LocomotionRunBackwardLeft,
    LocomotionRunLeft,
    LocomotionRunForwardLeft
};

enum class PlayerAnimEventType : uint8_t {
    None = 0,
    Footstep,
    Landing,
    SoundTrigger
};

const char *player_anim_state_name(PlayerAnimState state);
const char *player_anim_event_name(PlayerAnimEventType event_type);
bool player_anim_is_walk_cycle(PlayerAnimState state);
bool player_anim_is_run_cycle(PlayerAnimState state);
bool player_anim_is_locomotion_cycle(PlayerAnimState state);
PlayerAnimState player_anim_directional_locomotion_state(bool run, float direction_deg);
