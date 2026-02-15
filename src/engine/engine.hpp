#pragma once

#include "engine_core/timing.hpp"
#include "engine_gameplay/player/player_components.hpp"
#include "engine_input/input_state.hpp"
#include "engine_math/camera.hpp"
#include "engine_net/net_client.hpp"
#include "engine_physics/physics_world.hpp"
#include "engine_render/renderer.hpp"
#include "engine_world/physics/voxel_collision.hpp"
#include "engine_world/voxel_chunk.hpp"
#include "platform/platform.hpp"

class Engine {
public:
    void init(void *window_handle, RenderBackendType backend_type);
    void connect(const char *host, uint16_t port);
    void shutdown();
    void tick(double frame_dt);
    void set_input(const InputState &input, bool touch_mode);
    const RenderStats &stats() const;

private:
    void build_static_scene();
    void rebuild_dynamic_debug_mesh();
    void refresh_overlay_text();
    void update_third_person_camera();

    FixedStep fixed;
    Renderer renderer;
    PhysicsWorld physics;
    NetClient net_client;

    VoxelChunk world_chunk;
    VoxelCollisionWorld collision_world{nullptr};

    Camera camera;
    PlayerEntity local_player;
    ReplicatedPlayerMotion local_replication{};

    RenderScene scene;
    RenderStats render_stats;
    InputState input_state{};
    bool touch_input_mode = false;

    double fps_accumulator = 0.0;
    uint32_t fps_frames = 0;
    uint64_t frame_index = 0;

    NetSnapshot latest_snapshot{};
    bool has_snapshot = false;
};
