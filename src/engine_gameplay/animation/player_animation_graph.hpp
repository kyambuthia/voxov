#pragma once

#include "engine_gameplay/animation/player_animation_types.hpp"

#include <string>
#include <string_view>
#include <vector>

enum class AnimationLoopMode : uint8_t {
    Loop = 0,
    OneShot = 1
};

struct PlayerAnimationEventDefinition {
    float normalized_time = 0.0f;
    PlayerAnimEventType type = PlayerAnimEventType::None;
    std::string payload;
};

struct PlayerAnimationStateDefinition {
    PlayerAnimState state = PlayerAnimState::Idle;
    std::string label;
    std::vector<std::string> clip_aliases;
    std::vector<PlayerAnimState> fallback_states;
    std::vector<PlayerAnimationEventDefinition> events;
    AnimationLoopMode loop_mode = AnimationLoopMode::Loop;
    float phase_rate = 1.0f;
    float target_blend = 0.0f;
    float crossfade_seconds = 0.1f;
};

const PlayerAnimationStateDefinition &player_animation_definition(PlayerAnimState state);
const std::vector<PlayerAnimationStateDefinition> &player_animation_definitions();
bool player_animation_matches_alias(std::string_view clip_name, std::string_view alias);
