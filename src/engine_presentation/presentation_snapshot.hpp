#pragma once

#include "engine_assets/skinned_model.hpp"
#include "engine_gameplay/animation/animation_runtime.hpp"
#include "engine_gameplay/minigames/minigames.hpp"
#include "engine_gameplay/player/player_controller.hpp"
#include "engine_net/net_common.hpp"
#include "engine_physics/vehicle/ground_vehicle_controller.hpp"
#include "engine_render/render_types.hpp"
#include "engine_ui/gui_menu.hpp"
#include "engine_world/physics/voxel_collision.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <string>
#include <vector>

struct RuntimeHudSnapshot {
    GuiMenuView menu_view{};
    bool devhud_enabled = false;
    bool show_minigame_panel = false;
    bool show_hotspot_panel = false;
    bool show_objective_panel = true;
    float minigame_progress = 0.0f;
    std::string devhud_text;
    std::string minigame_title;
    std::string minigame_status;
    std::string minigame_objective;
    std::string minigame_controls;
    std::string minigame_hint;
    std::string hotspot_text;
    std::string objective_status;
    std::string objective_hint;
};

struct RuntimeMiniGameHotspotDebugSnapshot {
    MiniGameType type = MiniGameType::Snake;
    glm::vec3 position = glm::vec3(0.0f);
    bool nearby = false;
    bool active = false;
};

struct RuntimeObjectiveDebugSnapshot {
    glm::vec3 position = glm::vec3(0.0f);
    bool activated = false;
    bool nearby = false;
};

struct RuntimeRemoteDebugPlayerSnapshot {
    uint32_t player_id = 0;
    glm::vec3 position = glm::vec3(0.0f);
    glm::quat orientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    uint8_t anim_state = 0;
    float anim_phase = 0.0f;
    float anim_blend = 0.0f;
    const PlayerAnimationRuntime *animation_runtime = nullptr;
};

struct RuntimeVehicleDebugSnapshot {
    const GroundVehicleController *controller = nullptr;
    glm::vec3 position = glm::vec3(0.0f);
    float yaw = 0.0f;
    bool occupied = false;
};

struct RuntimeAircraftDebugSnapshot {
    glm::vec3 position = glm::vec3(0.0f);
    float yaw = 0.0f;
    bool occupied = false;
};

struct RuntimeDebugSceneSnapshot {
    bool collision_debug_enabled = false;
    bool debug_collision_only = false;
    bool devhud_enabled = false;
    bool splitscreen = false;
    bool spherical_planet = false;
    bool render_skeleton_only = false;
    const SkinnedModel *selected_player_model = nullptr;
    const VoxelCollisionWorld *collision_world = nullptr;
    const PlayerEntity *local_player = nullptr;
    const PlayerAnimationRuntime *local_player_animation = nullptr;
    const PlayerEntity *local_player_secondary = nullptr;
    const PlayerAnimationRuntime *local_player_secondary_animation = nullptr;
    PlayerCollisionDebug last_collision_debug{};
    RuntimeVehicleDebugSnapshot vehicle{};
    RuntimeAircraftDebugSnapshot aircraft{};
    glm::vec3 spherical_planet_center = glm::vec3(0.0f);
    float spherical_planet_radius = 0.0f;
    std::vector<RuntimeMiniGameHotspotDebugSnapshot> minigame_hotspots;
    std::vector<RuntimeObjectiveDebugSnapshot> objective_nodes;
    bool extraction_unlocked = false;
    bool objective_round_complete = false;
    glm::vec3 extraction_zone_position = glm::vec3(0.0f);
    float extraction_zone_radius = 0.0f;
    MiniGameState active_minigame{};
    int active_minigame_hotspot = -1;
    std::vector<RuntimeRemoteDebugPlayerSnapshot> remote_players;
};
