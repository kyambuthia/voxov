#pragma once

#include "engine_core/timing.hpp"
#include "engine_render/renderer.hpp"
#include "engine_physics/physics_world.hpp"
#include "engine_net/net_client.hpp"

class Engine {
public:
    void init(void *window_handle);
    void connect(const char *host, uint16_t port);
    void shutdown();
    void tick(double frame_dt);
    void set_input(float move_x, float move_y);

private:
    FixedStep fixed;
    Renderer renderer;
    PhysicsWorld physics;
    NetClient net_client;
    uint64_t frame_index = 0;
    NetSnapshot latest_snapshot{};
    bool has_snapshot = false;
    float input_move_x = 0.0f;
    float input_move_y = 0.0f;
};
