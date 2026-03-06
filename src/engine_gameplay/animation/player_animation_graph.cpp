#include "engine_gameplay/animation/player_animation_graph.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace {
using StateDef = PlayerAnimationStateDefinition;
using EventDef = PlayerAnimationEventDefinition;

std::string lower_copy(std::string_view value) {
    std::string out(value.begin(), value.end());
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

const std::vector<StateDef> &definitions_storage() {
    static const std::vector<StateDef> defs = {
        {
            PlayerAnimState::Idle,
            "idle",
            {"idle", "stand", "breath", "survey"},
            {},
            {
                EventDef{0.18f, PlayerAnimEventType::SoundTrigger, "cloth_idle"},
            },
            AnimationLoopMode::Loop,
            1.0f,
            0.0f,
            0.10f,
        },
        {
            PlayerAnimState::StartMove,
            "start_move",
            {"start_move", "move_start", "walk_start", "run_start"},
            {PlayerAnimState::LocomotionWalk, PlayerAnimState::LocomotionRun},
            {
                EventDef{0.42f, PlayerAnimEventType::Footstep, "start_step"},
            },
            AnimationLoopMode::OneShot,
            4.5f,
            0.38f,
            0.05f,
        },
        {
            PlayerAnimState::StopMove,
            "stop_move",
            {"stop_move", "move_stop", "brake", "halt"},
            {PlayerAnimState::Idle},
            {
                EventDef{0.22f, PlayerAnimEventType::Footstep, "stop_step"},
            },
            AnimationLoopMode::OneShot,
            3.8f,
            0.18f,
            0.06f,
        },
        {
            PlayerAnimState::LocomotionWalk,
            "locomotion_walk",
            {"locomotion_walk", "walk", "jog"},
            {},
            {
                EventDef{0.14f, PlayerAnimEventType::Footstep, "left"},
                EventDef{0.62f, PlayerAnimEventType::Footstep, "right"},
            },
            AnimationLoopMode::Loop,
            4.2f,
            0.45f,
            0.08f,
        },
        {
            PlayerAnimState::LocomotionRun,
            "locomotion_run",
            {"locomotion_run", "run", "sprint"},
            {PlayerAnimState::LocomotionWalk},
            {
                EventDef{0.12f, PlayerAnimEventType::Footstep, "left"},
                EventDef{0.56f, PlayerAnimEventType::Footstep, "right"},
                EventDef{0.12f, PlayerAnimEventType::SoundTrigger, "run_step"},
                EventDef{0.56f, PlayerAnimEventType::SoundTrigger, "run_step"},
            },
            AnimationLoopMode::Loop,
            6.2f,
            0.95f,
            0.07f,
        },
        {
            PlayerAnimState::JumpTakeoff,
            "jump_takeoff",
            {"jump_takeoff", "jump_start", "takeoff", "jump"},
            {PlayerAnimState::JumpLoop},
            {
                EventDef{0.08f, PlayerAnimEventType::SoundTrigger, "jump"},
            },
            AnimationLoopMode::OneShot,
            5.6f,
            0.72f,
            0.05f,
        },
        {
            PlayerAnimState::JumpLoop,
            "jump_loop",
            {"jump_loop", "airborne_rise", "rise", "jump_up"},
            {PlayerAnimState::FallLoop},
            {},
            AnimationLoopMode::Loop,
            2.0f,
            0.66f,
            0.05f,
        },
        {
            PlayerAnimState::FallLoop,
            "fall_loop",
            {"fall_loop", "airborne_fall", "fall", "airborne"},
            {PlayerAnimState::JumpLoop},
            {},
            AnimationLoopMode::Loop,
            2.2f,
            0.62f,
            0.05f,
        },
        {
            PlayerAnimState::LandSoft,
            "land_soft",
            {"land_soft", "land", "landing"},
            {PlayerAnimState::Idle, PlayerAnimState::LocomotionWalk},
            {
                EventDef{0.08f, PlayerAnimEventType::Landing, "soft"},
                EventDef{0.08f, PlayerAnimEventType::SoundTrigger, "land_soft"},
            },
            AnimationLoopMode::OneShot,
            5.3f,
            0.38f,
            0.05f,
        },
        {
            PlayerAnimState::LandHard,
            "land_hard",
            {"land_hard", "hard_land", "heavy_land", "stumble"},
            {PlayerAnimState::Recovery, PlayerAnimState::Idle},
            {
                EventDef{0.08f, PlayerAnimEventType::Landing, "hard"},
                EventDef{0.08f, PlayerAnimEventType::SoundTrigger, "land_hard"},
            },
            AnimationLoopMode::OneShot,
            4.6f,
            0.78f,
            0.05f,
        },
        {
            PlayerAnimState::PivotLeft,
            "pivot_left",
            {"pivot_left", "turn_start_left", "pivot"},
            {PlayerAnimState::LocomotionWalk, PlayerAnimState::LocomotionRun},
            {
                EventDef{0.18f, PlayerAnimEventType::Footstep, "pivot_left"},
            },
            AnimationLoopMode::OneShot,
            4.4f,
            0.58f,
            0.06f,
        },
        {
            PlayerAnimState::PivotRight,
            "pivot_right",
            {"pivot_right", "turn_start_right", "pivot"},
            {PlayerAnimState::LocomotionWalk, PlayerAnimState::LocomotionRun},
            {
                EventDef{0.18f, PlayerAnimEventType::Footstep, "pivot_right"},
            },
            AnimationLoopMode::OneShot,
            4.4f,
            0.58f,
            0.06f,
        },
        {
            PlayerAnimState::TurnInPlaceLeft,
            "turn_in_place_left",
            {"turn_in_place_left", "turn_left", "idle_turn_left"},
            {PlayerAnimState::Idle},
            {
                EventDef{0.26f, PlayerAnimEventType::Footstep, "turn_left"},
            },
            AnimationLoopMode::Loop,
            2.6f,
            0.32f,
            0.06f,
        },
        {
            PlayerAnimState::TurnInPlaceRight,
            "turn_in_place_right",
            {"turn_in_place_right", "turn_right", "idle_turn_right"},
            {PlayerAnimState::Idle},
            {
                EventDef{0.26f, PlayerAnimEventType::Footstep, "turn_right"},
            },
            AnimationLoopMode::Loop,
            2.6f,
            0.32f,
            0.06f,
        },
        {
            PlayerAnimState::MovingTurn,
            "moving_turn",
            {"moving_turn", "strafe_turn", "run_turn"},
            {PlayerAnimState::LocomotionWalk, PlayerAnimState::LocomotionRun},
            {
                EventDef{0.16f, PlayerAnimEventType::Footstep, "left"},
                EventDef{0.60f, PlayerAnimEventType::Footstep, "right"},
            },
            AnimationLoopMode::Loop,
            4.8f,
            0.72f,
            0.06f,
        },
        {
            PlayerAnimState::Recovery,
            "recovery",
            {"recovery", "recover", "slide", "stumble"},
            {PlayerAnimState::Idle},
            {
                EventDef{0.10f, PlayerAnimEventType::SoundTrigger, "recover"},
            },
            AnimationLoopMode::OneShot,
            3.0f,
            0.30f,
            0.08f,
        },
    };
    return defs;
}
}

const PlayerAnimationStateDefinition &player_animation_definition(PlayerAnimState state) {
    const std::vector<StateDef> &defs = definitions_storage();
    const auto it = std::find_if(defs.begin(), defs.end(), [state](const StateDef &def) {
        return def.state == state;
    });
    if (it == defs.end()) {
        throw std::runtime_error("missing player animation definition");
    }
    return *it;
}

const std::vector<PlayerAnimationStateDefinition> &player_animation_definitions() {
    return definitions_storage();
}

bool player_animation_matches_alias(std::string_view clip_name, std::string_view alias) {
    if (clip_name.empty() || alias.empty()) {
        return false;
    }
    const std::string lowered_name = lower_copy(clip_name);
    const std::string lowered_alias = lower_copy(alias);
    return lowered_name.find(lowered_alias) != std::string::npos;
}
