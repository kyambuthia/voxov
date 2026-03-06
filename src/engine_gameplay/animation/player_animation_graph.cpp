#include "engine_gameplay/animation/player_animation_graph.hpp"

#include <algorithm>
#include <array>
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
        StateDef{
            PlayerAnimState::Idle,
            "idle",
            {"idle", "stand", "survey", "breath"},
            {},
            {
                EventDef{0.15f, PlayerAnimEventType::SoundTrigger, "cloth_idle"},
            },
            AnimationLoopMode::Loop,
            1.0f,
            0.0f,
            0.12f,
        },
        StateDef{
            PlayerAnimState::Push,
            "push",
            {"push", "run", "skate_push"},
            {PlayerAnimState::Cruise},
            {
                EventDef{0.16f, PlayerAnimEventType::BoardContact, "rear_push"},
                EventDef{0.18f, PlayerAnimEventType::SoundTrigger, "push_foot"},
                EventDef{0.63f, PlayerAnimEventType::BoardContact, "front_settle"},
            },
            AnimationLoopMode::Loop,
            7.4f,
            0.95f,
            0.08f,
        },
        StateDef{
            PlayerAnimState::Cruise,
            "cruise",
            {"cruise", "skate", "ride", "walk", "run"},
            {PlayerAnimState::Push},
            {
                EventDef{0.12f, PlayerAnimEventType::BoardContact, "front_truck"},
                EventDef{0.58f, PlayerAnimEventType::BoardContact, "rear_truck"},
            },
            AnimationLoopMode::Loop,
            4.8f,
            0.58f,
            0.10f,
        },
        StateDef{
            PlayerAnimState::TurnLeft,
            "turn_left",
            {"turn_left", "left_turn", "walk", "run"},
            {PlayerAnimState::Cruise},
            {
                EventDef{0.14f, PlayerAnimEventType::BoardContact, "lean_left"},
            },
            AnimationLoopMode::Loop,
            4.4f,
            0.62f,
            0.08f,
        },
        StateDef{
            PlayerAnimState::TurnRight,
            "turn_right",
            {"turn_right", "right_turn", "walk", "run"},
            {PlayerAnimState::Cruise},
            {
                EventDef{0.14f, PlayerAnimEventType::BoardContact, "lean_right"},
            },
            AnimationLoopMode::Loop,
            4.4f,
            0.62f,
            0.08f,
        },
        StateDef{
            PlayerAnimState::Ollie,
            "ollie",
            {"ollie", "jump", "hop"},
            {PlayerAnimState::Airborne},
            {
                EventDef{0.08f, PlayerAnimEventType::BoardContact, "pop"},
                EventDef{0.42f, PlayerAnimEventType::TrickApex, "ollie_apex"},
                EventDef{0.10f, PlayerAnimEventType::SoundTrigger, "ollie_pop"},
            },
            AnimationLoopMode::OneShot,
            6.0f,
            0.78f,
            0.06f,
        },
        StateDef{
            PlayerAnimState::Kickflip,
            "kickflip",
            {"kickflip"},
            {PlayerAnimState::Ollie, PlayerAnimState::Airborne},
            {
                EventDef{0.18f, PlayerAnimEventType::BoardContact, "kickflip_flick"},
                EventDef{0.52f, PlayerAnimEventType::TrickApex, "kickflip_apex"},
                EventDef{0.18f, PlayerAnimEventType::SoundTrigger, "kickflip"},
            },
            AnimationLoopMode::OneShot,
            6.6f,
            0.9f,
            0.05f,
        },
        StateDef{
            PlayerAnimState::ShoveIt,
            "shove_it",
            {"shove_it", "shove-it", "shove", "pop_shove"},
            {PlayerAnimState::Ollie, PlayerAnimState::Airborne},
            {
                EventDef{0.18f, PlayerAnimEventType::BoardContact, "shove_it_pop"},
                EventDef{0.50f, PlayerAnimEventType::TrickApex, "shove_it_apex"},
                EventDef{0.18f, PlayerAnimEventType::SoundTrigger, "shove_it"},
            },
            AnimationLoopMode::OneShot,
            6.4f,
            0.88f,
            0.05f,
        },
        StateDef{
            PlayerAnimState::Manual,
            "manual",
            {"manual", "balance", "crouch"},
            {PlayerAnimState::Cruise, PlayerAnimState::Idle},
            {
                EventDef{0.25f, PlayerAnimEventType::SoundTrigger, "manual_shift"},
            },
            AnimationLoopMode::Loop,
            3.1f,
            0.46f,
            0.10f,
        },
        StateDef{
            PlayerAnimState::GrindEnter,
            "grind_enter",
            {"grind_enter", "grind_start", "grind"},
            {PlayerAnimState::Airborne, PlayerAnimState::Cruise},
            {
                EventDef{0.10f, PlayerAnimEventType::BoardContact, "grind_lock"},
                EventDef{0.10f, PlayerAnimEventType::SoundTrigger, "grind_spark"},
            },
            AnimationLoopMode::OneShot,
            5.3f,
            0.82f,
            0.05f,
        },
        StateDef{
            PlayerAnimState::GrindLoop,
            "grind_loop",
            {"grind_loop", "grind"},
            {PlayerAnimState::Cruise},
            {
                EventDef{0.15f, PlayerAnimEventType::BoardContact, "grind_slide"},
                EventDef{0.15f, PlayerAnimEventType::SoundTrigger, "grind_spark"},
                EventDef{0.65f, PlayerAnimEventType::BoardContact, "grind_slide"},
            },
            AnimationLoopMode::Loop,
            4.5f,
            0.72f,
            0.06f,
        },
        StateDef{
            PlayerAnimState::GrindExit,
            "grind_exit",
            {"grind_exit", "grind_end", "grind"},
            {PlayerAnimState::Land},
            {
                EventDef{0.10f, PlayerAnimEventType::BoardContact, "grind_release"},
                EventDef{0.12f, PlayerAnimEventType::SoundTrigger, "grind_exit"},
            },
            AnimationLoopMode::OneShot,
            5.1f,
            0.76f,
            0.05f,
        },
        StateDef{
            PlayerAnimState::Airborne,
            "airborne",
            {"airborne", "jump", "fall"},
            {PlayerAnimState::Ollie, PlayerAnimState::Idle},
            {
                EventDef{0.50f, PlayerAnimEventType::TrickApex, "airborne_apex"},
            },
            AnimationLoopMode::Loop,
            2.2f,
            0.7f,
            0.06f,
        },
        StateDef{
            PlayerAnimState::Land,
            "land",
            {"land", "landing"},
            {PlayerAnimState::Idle, PlayerAnimState::Cruise},
            {
                EventDef{0.08f, PlayerAnimEventType::Landing, "landing"},
                EventDef{0.08f, PlayerAnimEventType::SoundTrigger, "land"},
            },
            AnimationLoopMode::OneShot,
            5.8f,
            0.42f,
            0.05f,
        },
        StateDef{
            PlayerAnimState::Bail,
            "bail",
            {"bail", "fall", "crash", "ragdoll"},
            {PlayerAnimState::Airborne},
            {
                EventDef{0.14f, PlayerAnimEventType::SoundTrigger, "bail"},
                EventDef{0.18f, PlayerAnimEventType::Landing, "slam"},
            },
            AnimationLoopMode::OneShot,
            2.6f,
            1.0f,
            0.04f,
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
