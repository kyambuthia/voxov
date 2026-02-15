#include "engine/engine.hpp"
#include "engine_render/debug_text.hpp"

#include <cstdio>

void Engine::init(void *window_handle, RenderBackendType backend_type) {
    EnginePhysicsSettings settings{};
    physics.init(settings);
    net_client.init();

    camera.transform.position = glm::vec3(8.0f, 12.0f, 28.0f);
    camera.transform.euler_radians = glm::vec3(glm::radians(-18.0f), glm::radians(180.0f), 0.0f);
    build_demo_scene();
    refresh_overlay_text();

    try {
        renderer.init(window_handle, backend_type);
        renderer.upload_scene(scene);
        renderer.update_overlay_text(scene.overlay_text);
    } catch (const std::exception &e) {
        std::fprintf(stderr, "Renderer init failed: %s\n", e.what());
        throw;
    }
}

void Engine::connect(const char *host, uint16_t port) {
    net_client.connect(host, port);
    NetChunkInterest interest{};
    interest.center_x = 0;
    interest.center_z = 0;
    interest.radius = 2;
    net_client.set_chunk_interest(interest);
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
    const float camera_speed = 8.0f;
    camera.transform.position += camera.right() * (input_move_x * camera_speed * static_cast<float>(frame_dt));
    camera.transform.position += camera.forward() * (input_move_y * camera_speed * static_cast<float>(frame_dt));

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

    fps_accumulator += frame_dt;
    fps_frames++;
    if (fps_accumulator >= 0.3) {
        render_stats.fps = static_cast<double>(fps_frames) / fps_accumulator;
        render_stats.cpu_ms = (fps_accumulator * 1000.0) / static_cast<double>(fps_frames);
        fps_accumulator = 0.0;
        fps_frames = 0;
        refresh_overlay_text();
        renderer.update_overlay_text(scene.overlay_text);
    }

    RenderFrameContext ctx{};
    ctx.frame_index = frame_index++;
    ctx.alpha = fixed.accumulator / fixed.fixed_dt;
    ctx.delta_seconds = frame_dt;
    ctx.aspect_ratio = 16.0f / 9.0f;

    renderer.begin_frame(ctx, camera, render_stats);
    renderer.end_frame();
}

const RenderStats &Engine::stats() const {
    return render_stats;
}

void Engine::build_demo_scene() {
    world_chunk.generate_heightmap_terrain();

    scene = RenderScene{};
    scene.opaque_meshes.push_back(world_chunk.build_sky_placeholder(240.0f));
    scene.opaque_meshes.push_back(world_chunk.build_naive_mesh());
    scene.debug_grid = world_chunk.build_debug_grid(96.0f, 1.0f);
}

void Engine::refresh_overlay_text() {
    char text[96]{};
    std::snprintf(text, sizeof(text), "FPS %.1f CPU %.2fMS", render_stats.fps, render_stats.cpu_ms);
    scene.overlay_text = build_camera_text_mesh(camera, text);
}
