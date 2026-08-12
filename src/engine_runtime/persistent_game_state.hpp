#pragma once

#include "engine_gameplay/objectives/expedition_mission.hpp"
#include "engine_ui/gui_menu.hpp"
#include "engine_world/planet_blocks.hpp"
#include "platform/platform_services.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

struct PersistentBlockEdit {
    int32_t body_index = 1;
    BlockAddress address{};
    VoxelMaterial material = VoxelMaterial::Air;
    bool solid = false;

    friend bool operator==(const PersistentBlockEdit &,
                           const PersistentBlockEdit &) = default;
};

struct PersistentGameState {
    static constexpr uint16_t kFormatVersion = 1;
    static constexpr uint16_t kGeneratorVersion = 1;

    ExpeditionStage expedition_stage = ExpeditionStage::CollectSample;
    GuiMenu::Character character = GuiMenu::Character::Capsule;
    int32_t active_body_index = 1;
    glm::dvec3 player_local_position{0.0};
    std::vector<PersistentBlockEdit> block_edits;
};

bool encode_persistent_game_state(
    const PersistentGameState &state,
    std::vector<uint8_t> &out,
    std::string &out_error);
bool decode_persistent_game_state(
    const std::vector<uint8_t> &bytes,
    PersistentGameState &out,
    std::string &out_error);
bool save_persistent_game_state(
    const PlatformServices &platform,
    const PersistentGameState &state,
    std::string &out_error);
bool load_persistent_game_state(
    const PlatformServices &platform,
    PersistentGameState &out,
    std::string &out_error);
