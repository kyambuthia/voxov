#pragma once

#include "engine_core/timing.hpp"
#include "engine_gameplay/player/player_components.hpp"
#include "engine_gameplay/player/player_controller.hpp"
#include "engine_input/input_state.hpp"
#include "engine_audio/ui_audio.hpp"
#include "engine_assets/skinned_model.hpp"
#include "engine_math/camera.hpp"
#include "engine_net/net_client.hpp"
#include "engine_net/lan_discovery.hpp"
#include "engine_net/net_server.hpp"
#include "engine_physics/physics_world.hpp"
#include "engine_render/renderer.hpp"
#include "engine_ui/gui_menu.hpp"
#include "engine_world/physics/voxel_collision.hpp"
#include "engine_world/voxel_chunk.hpp"
#include "platform/platform.hpp"

#include <unordered_map>
#include <array>

struct EngineRuntimeOptions {
    bool devhud = false;
    bool noclip = false;
    bool splitscreen = false;
    bool debug_collision = false;
    bool debug_xray = false;
    bool debug_collision_only = false;
    bool debug_freeze = false;
};

class Engine {
public:
    void init(void *window_handle, RenderBackendType backend_type, const EngineRuntimeOptions &options);
    void connect(const char *host, uint16_t port);
    void shutdown();
    void tick(double frame_dt);
    void set_input(const InputState &input_primary, const InputState &input_secondary, bool touch_mode);
    const RenderStats &stats() const;

private:
    struct RemoteRenderPlayer {
        glm::vec3 position = glm::vec3(0.0f);
        glm::vec3 target_position = glm::vec3(0.0f);
        glm::vec3 velocity = glm::vec3(0.0f);
        glm::quat orientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        glm::quat target_orientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        uint8_t anim_state = 0;
        float anim_phase = 0.0f;
        float anim_blend = 0.0f;
        bool initialized = false;
    };
    struct VehicleState {
        glm::vec3 position = glm::vec3(10.0f, 0.0f, 10.0f);
        float yaw = 0.0f;
        float speed = 0.0f;
        bool occupied = false;
    };

    void build_static_scene();
    void handle_vehicle_interaction(const InputState &input);
    void update_vehicle_sim(const InputState &input, float dt);
    glm::vec3 vehicle_seat_world_position() const;
    void rebuild_dynamic_debug_mesh();
    void refresh_overlay_text();
    void update_third_person_camera(PlayerEntity &player, Camera &out_camera);
    void update_third_person_camera(PlayerEntity &player, const glm::vec3 &render_position, Camera &out_camera);
    void sync_network_state(uint32_t sim_tick, const InputState &net_input);
    void start_local_server(uint16_t port, bool loopback_only);
    void record_prediction_history(uint32_t sim_tick, const InputState &step_input);
    void reconcile_local_player_from_snapshot(uint32_t current_sim_tick);

    FixedStep fixed;
    Renderer renderer;
    PhysicsWorld physics;
    NetClient net_client;
    LanDiscovery lan_discovery;
    NetServer local_server;
    bool local_server_loopback = true;
    bool local_server_running = false;
    bool searching_nearby = false;
    std::string multiplayer_hint;
    bool gameplay_started = false;

    VoxelChunk world_chunk;
    VoxelCollisionWorld collision_world{nullptr};

    Camera camera;
    Camera secondary_camera;
    PlayerEntity local_player;
    PlayerEntity local_player_secondary;
    glm::vec3 local_player_prev_position = glm::vec3(0.0f);
    glm::vec3 local_player_secondary_prev_position = glm::vec3(0.0f);
    ReplicatedPlayerMotion local_replication{};
    std::unordered_map<uint32_t, NetPlayerState> remote_players;
    std::unordered_map<uint32_t, RemoteRenderPlayer> remote_render_players;

    RenderScene scene;
    RenderStats render_stats;
    InputState input_state{};
    InputState input_state_secondary{};
    bool touch_input_mode = false;
    GuiMenu gui_menu;
    UiAudio ui_audio;
    SkinnedModel fox_player_model;
    bool has_fox_player_model = false;
    SkinnedModel humanoid_player_model;
    bool has_humanoid_player_model = false;
    EngineRuntimeOptions runtime_options{};

    double fps_accumulator = 0.0;
    uint32_t fps_frames = 0;
    uint64_t frame_index = 0;
    double log_accumulator = 0.0;
    double last_frame_dt = 0.0;

    NetSnapshot latest_snapshot{};
    bool has_snapshot = false;
    struct PredictionHistoryEntry {
        bool valid = false;
        uint32_t tick = 0;
        InputState input{};
        glm::vec3 position = glm::vec3(0.0f);
        glm::vec3 velocity = glm::vec3(0.0f);
    };
    static constexpr size_t k_prediction_history_size = 512;
    std::array<PredictionHistoryEntry, k_prediction_history_size> prediction_history{};
    float last_reconcile_pos_error = 0.0f;
    uint32_t last_reconcile_snapshot_tick = 0;
    uint32_t reconcile_replay_ticks = 0;
    uint64_t reconcile_corrections = 0;
    PlayerCollisionDebug last_collision_debug{};
    PlayerCollisionDebug last_collision_debug_secondary{};
    RenderMesh frozen_debug_world{};
    VehicleState vehicle{};
};
