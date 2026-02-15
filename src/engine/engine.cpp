#include "engine/engine.hpp"

#include "engine_gameplay/player/player_controller.hpp"
#include "engine_render/debug_draw/debug_draw.hpp"
#include "engine_render/debug_text.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
float to_radians(float deg) {
    return deg * 0.01745329251994329577f;
}
}

void Engine::init(void *window_handle, RenderBackendType backend_type) {
    EnginePhysicsSettings settings{};
    physics.init(settings);
    net_client.init();

    build_static_scene();
    local_player = PlayerControllerSystem::spawn_player(collision_world);
    update_third_person_camera();
    refresh_overlay_text();
    rebuild_dynamic_debug_mesh();

    local_replication.network_id = local_player.network_id;
    local_replication.position = local_player.transform.position;
    local_replication.velocity = local_player.controller.velocity;

    spdlog::info("Spawned player id={} at ({:.2f}, {:.2f}, {:.2f})",
        local_player.network_id,
        local_player.transform.position.x,
        local_player.transform.position.y,
        local_player.transform.position.z);

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

void Engine::set_input(const InputState &input, bool touch_mode) {
    input_state = input;
    touch_input_mode = touch_mode;
}

void Engine::shutdown() {
    renderer.shutdown();
    net_client.disconnect();
    net_client.shutdown();
    physics.shutdown();
}

void Engine::tick(double frame_dt) {
    PlayerControllerSystem::update_camera_rig(local_player, input_state, touch_input_mode, static_cast<float>(frame_dt));

    fixed.accumulator += frame_dt;
    bool jump_consumed = false;

    while (fixed.accumulator >= fixed.fixed_dt) {
        InputState step_input = input_state;
        if (jump_consumed) {
            step_input.jump_pressed = false;
        }

        PlayerControllerSystem::simulate_fixed(
            local_player,
            step_input,
            collision_world,
            static_cast<float>(fixed.fixed_dt));
        jump_consumed = jump_consumed || input_state.jump_pressed;

        NetTickInput input{};
        input.tick = static_cast<uint32_t>(fixed.tick);
        input.move_x = step_input.move.x;
        input.move_y = step_input.move.y;
        net_client.send_input(input);
        net_client.pump();
        if (net_client.poll_snapshot(latest_snapshot)) {
            has_snapshot = true;
        }

        local_replication.position = local_player.transform.position;
        local_replication.velocity = local_player.controller.velocity;

        physics.step(static_cast<float>(fixed.fixed_dt));
        fixed.accumulator -= fixed.fixed_dt;
        fixed.tick++;
    }

    input_state.jump_pressed = false;

    update_third_person_camera();

    fps_accumulator += frame_dt;
    fps_frames++;
    if (fps_accumulator >= 0.3) {
        render_stats.fps = static_cast<double>(fps_frames) / fps_accumulator;
        render_stats.cpu_ms = (fps_accumulator * 1000.0) / static_cast<double>(fps_frames);
        fps_accumulator = 0.0;
        fps_frames = 0;
    }

    refresh_overlay_text();
    rebuild_dynamic_debug_mesh();
    renderer.update_overlay_text(scene.overlay_text);

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

void Engine::build_static_scene() {
    world_chunk.generate_heightmap_terrain();
    collision_world = VoxelCollisionWorld(&world_chunk);

    scene = RenderScene{};
    scene.opaque_meshes.push_back(world_chunk.build_sky_placeholder(240.0f));
    scene.opaque_meshes.push_back(world_chunk.build_naive_mesh());
    scene.debug_grid = world_chunk.build_debug_grid(96.0f, 1.0f);
}

void Engine::update_third_person_camera() {
    const glm::vec3 pivot = local_player.transform.position + glm::vec3(0.0f, local_player.camera_rig.pivotHeight, 0.0f);

    const float yaw = to_radians(local_player.camera_rig.yaw);
    const float pitch = to_radians(local_player.camera_rig.pitch);
    const glm::vec3 orbit_forward = glm::normalize(glm::vec3(
        std::sin(yaw) * std::cos(pitch),
        std::sin(pitch),
        std::cos(yaw) * std::cos(pitch)));

    float camera_distance = local_player.camera_rig.distance;
    float hit_distance = 0.0f;
    if (collision_world.raycast(pivot, -orbit_forward, local_player.camera_rig.distance, hit_distance)) {
        camera_distance = std::max(local_player.camera_rig.minDistance, hit_distance - 0.15f);
    }

    const glm::vec3 camera_pos = pivot - orbit_forward * camera_distance;
    const glm::vec3 view_dir = glm::normalize(pivot - camera_pos);

    camera.transform.position = camera_pos;
    camera.transform.euler_radians.y = std::atan2(view_dir.x, view_dir.z);
    camera.transform.euler_radians.x = std::asin(std::clamp(view_dir.y, -1.0f, 1.0f));
    camera.transform.euler_radians.z = 0.0f;
}

void Engine::refresh_overlay_text() {
    char text[128]{};
    std::snprintf(
        text,
        sizeof(text),
        "FPS %05.1f CPU %05.2fMS G %d",
        render_stats.fps,
        render_stats.cpu_ms,
        local_player.controller.grounded ? 1 : 0);
    scene.overlay_text = build_camera_text_mesh(camera, text);
}

void Engine::rebuild_dynamic_debug_mesh() {
    RenderMesh player_capsule = build_debug_capsule_mesh(
        local_player.transform.position,
        local_player.controller.capsuleRadius,
        local_player.controller.capsuleHeight,
        glm::vec3(0.95f, 0.5f, 0.2f));

    RenderMesh target_marker = build_debug_sphere_mesh(
        local_player.transform.position + glm::vec3(0.0f, local_player.camera_rig.pivotHeight, 0.0f),
        0.12f,
        glm::vec3(0.2f, 0.85f, 1.0f));

    append_mesh(scene.overlay_text, player_capsule);
    append_mesh(scene.overlay_text, target_marker);
}
