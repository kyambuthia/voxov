#pragma once

#include "engine_core/timing.hpp"
#include "engine_math/camera.hpp"
#include "engine_render/renderer.hpp"
#include "engine_physics/physics_world.hpp"
#include "engine_net/net_client.hpp"
#include "engine_world/voxel_chunk.hpp"
#include "platform/platform.hpp"

class Engine {
public:
    void init(void *window_handle, RenderBackendType backend_type);
    void connect(const char *host, uint16_t port);
    void shutdown();
    void tick(double frame_dt);
    void set_input(float move_x, float move_y);
    const RenderStats &stats() const;

private:
    void build_demo_scene();
    void refresh_overlay_text();

    FixedStep fixed;
    Renderer renderer;
    PhysicsWorld physics;
    NetClient net_client;
    VoxelChunk world_chunk;
    Camera camera;
    RenderScene scene;
    RenderStats render_stats;
    double fps_accumulator = 0.0;
    uint32_t fps_frames = 0;
    uint64_t frame_index = 0;
    NetSnapshot latest_snapshot{};
    bool has_snapshot = false;
    float input_move_x = 0.0f;
    float input_move_y = 0.0f;
};
