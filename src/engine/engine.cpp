#include "engine/engine.hpp"

void Engine::init(void *window_handle) {
    EnginePhysicsSettings settings{};
    physics.init(settings);
    net_client.init();
    renderer.init(window_handle);
}

void Engine::connect(const char *host, uint16_t port) {
    net_client.connect(host, port);
}

void Engine::set_input(float move_x, float move_y) {
    input_move_x = move_x;
    input_move_y = move_y;
}

void Engine::shutdown() {
    renderer.shutdown();
    net_client.disconnect();
    net_client.shutdown();
    physics.shutdown();
}

void Engine::tick(double frame_dt) {
    fixed.accumulator += frame_dt;
    while (fixed.accumulator >= fixed.fixed_dt) {
        NetTickInput input{};
        input.tick = static_cast<uint32_t>(fixed.tick);
        input.move_x = input_move_x;
        input.move_y = input_move_y;
        net_client.send_input(input);
        net_client.pump();
        if (net_client.poll_snapshot(latest_snapshot)) {
            has_snapshot = true;
        }
        physics.step(static_cast<float>(fixed.fixed_dt));
        fixed.accumulator -= fixed.fixed_dt;
        fixed.tick++;
    }

    RenderFrameContext ctx{};
    ctx.frame_index = frame_index++;
    ctx.alpha = fixed.accumulator / fixed.fixed_dt;
    renderer.begin_frame(ctx);
    renderer.render_world();
    renderer.end_frame();
}
