#include "engine/engine.hpp"

#include "engine_gameplay/player/player_controller.hpp"
#include "engine_render/debug_draw/debug_draw.hpp"
#include "engine_render/debug_text.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

void Engine::init(void *window_handle, RenderBackendType backend_type, const EngineRuntimeOptions &options) {
    runtime_options = options;

    EnginePhysicsSettings settings{};
    physics.init(settings);
    net_client.init();

    build_static_scene();
    local_player = PlayerControllerSystem::spawn_player(collision_world);
    if (runtime_options.splitscreen) {
        local_player_secondary = PlayerControllerSystem::spawn_player(collision_world);
        local_player_secondary.network_id = 2;
        local_player_secondary.transform.position.x += 2.5f;
        local_player_secondary.camera_rig.yaw = 180.0f;
    }
    update_third_person_camera(local_player, camera);
    if (runtime_options.splitscreen) {
        update_third_person_camera(local_player_secondary, secondary_camera);
    }
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

void Engine::set_input(const InputState &input_primary, const InputState &input_secondary, bool touch_mode) {
    input_state = input_primary;
    input_state_secondary = input_secondary;
    touch_input_mode = touch_mode;
}

void Engine::shutdown() {
    renderer.shutdown();
    net_client.disconnect();
    net_client.shutdown();
    physics.shutdown();
}

void Engine::sync_network_state(uint32_t sim_tick) {
    NetTickInput input{};
    input.tick = sim_tick;
    input.move_x = input_state.move.x;
    input.move_y = input_state.move.y;
    net_client.send_input(input);

    net_client.pump();

    uint32_t assigned_id = net_client.local_player_id();
    if (assigned_id != 0 && local_player.network_id != assigned_id) {
        local_player.network_id = assigned_id;
        local_replication.network_id = assigned_id;
        spdlog::info("Assigned network player id={}", assigned_id);
    }

    if (net_client.poll_snapshot(latest_snapshot)) {
        has_snapshot = true;
    }

    remote_players.clear();
    for (const auto &[player_id, state] : net_client.player_states()) {
        if (player_id == local_player.network_id) {
            continue;
        }
        remote_players[player_id] = state;
    }
}

void Engine::tick(double frame_dt) {
    last_frame_dt = frame_dt;

    GuiMenuActions menu_actions{};
    gui_menu.handle_input(input_state, runtime_options.devhud, runtime_options.noclip, menu_actions);
    if (menu_actions.toggle_devhud) {
        runtime_options.devhud = !runtime_options.devhud;
    }
    if (menu_actions.toggle_noclip) {
        runtime_options.noclip = !runtime_options.noclip;
    }
    if (menu_actions.reset_camera) {
        local_player.camera_rig.yaw = 180.0f;
        local_player.camera_rig.pitch = -12.0f;
        local_player.camera_rig.distance = 5.0f;
    }

    InputState gameplay_input = input_state;
    if (gui_menu.open()) {
        gameplay_input.move = glm::vec2(0.0f);
        gameplay_input.look_delta = glm::vec2(0.0f);
        gameplay_input.jump_pressed = false;
        gameplay_input.jump_held = false;
        gameplay_input.sprint_held = false;
    }

    InputState gameplay_input_secondary = input_state_secondary;
    if (gui_menu.open()) {
        gameplay_input_secondary.move = glm::vec2(0.0f);
        gameplay_input_secondary.look_delta = glm::vec2(0.0f);
        gameplay_input_secondary.jump_pressed = false;
        gameplay_input_secondary.jump_held = false;
        gameplay_input_secondary.sprint_held = false;
    }

    PlayerControllerSystem::update_camera_rig(local_player, gameplay_input, touch_input_mode, static_cast<float>(frame_dt));
    if (runtime_options.splitscreen) {
        PlayerControllerSystem::update_camera_rig(local_player_secondary, gameplay_input_secondary, false, static_cast<float>(frame_dt));
    }

    fixed.accumulator += frame_dt;
    bool jump_consumed = false;

    while (fixed.accumulator >= fixed.fixed_dt) {
        InputState step_input = gameplay_input;
        if (jump_consumed) {
            step_input.jump_pressed = false;
        }

        last_collision_debug = PlayerControllerSystem::simulate_fixed(
            local_player,
            step_input,
            collision_world,
            static_cast<float>(fixed.fixed_dt),
            runtime_options.noclip);

        if (runtime_options.splitscreen) {
            InputState step_input_secondary = gameplay_input_secondary;
            last_collision_debug_secondary = PlayerControllerSystem::simulate_fixed(
                local_player_secondary,
                step_input_secondary,
                collision_world,
                static_cast<float>(fixed.fixed_dt),
                runtime_options.noclip);
        }
        jump_consumed = jump_consumed || input_state.jump_pressed;

        sync_network_state(static_cast<uint32_t>(fixed.tick));

        local_replication.position = local_player.transform.position;
        local_replication.velocity = local_player.controller.velocity;

        physics.step(static_cast<float>(fixed.fixed_dt));
        fixed.accumulator -= fixed.fixed_dt;
        fixed.tick++;
    }

    input_state.jump_pressed = false;

    update_third_person_camera(local_player, camera);
    if (runtime_options.splitscreen) {
        update_third_person_camera(local_player_secondary, secondary_camera);
    }

    fps_accumulator += frame_dt;
    fps_frames++;
    if (fps_accumulator >= 0.3) {
        render_stats.fps = static_cast<double>(fps_frames) / fps_accumulator;
        render_stats.cpu_ms = (fps_accumulator * 1000.0) / static_cast<double>(fps_frames);
        fps_accumulator = 0.0;
        fps_frames = 0;
    }

    if (runtime_options.devhud) {
        const MovementDebug movement_debug = PlayerControllerSystem::compute_movement_vectors(local_player.camera_rig.yaw, input_state.move);

        const bool any_non_zero =
            std::fabs(input_state.look_delta.x) > 0.0001f ||
            std::fabs(input_state.look_delta.y) > 0.0001f ||
            std::fabs(input_state.move.x) > 0.0001f ||
            std::fabs(input_state.move.y) > 0.0001f ||
            input_state.key_w || input_state.key_a || input_state.key_s || input_state.key_d;

        log_accumulator += frame_dt;
        if (any_non_zero && log_accumulator >= 0.0) {
            log_accumulator = 0.0;
            spdlog::info(
                "devhud dt={:.4f} fixed_dt={:.4f} mouse_dx={:.2f} mouse_dy={:.2f} keys[W{} A{} S{} D{}] axes[MoveX={:.2f} MoveY={:.2f} LookX={:.2f} LookY={:.2f}] cam[yaw={:.2f} pitch={:.2f} f=({:.2f},{:.2f},{:.2f}) r=({:.2f},{:.2f},{:.2f})] move[desired=({:.2f},{:.2f},{:.2f}) strafeRight=({:.2f},{:.2f},{:.2f})] pos=({:.2f},{:.2f},{:.2f}) vel=({:.2f},{:.2f},{:.2f}) grounded={} pen={:.3f} n=({:.2f},{:.2f},{:.2f}) mode[rmb={} lock={} look_en={}] remotes={} noclip={}",
                frame_dt,
                fixed.fixed_dt,
                input_state.look_delta.x,
                input_state.look_delta.y,
                input_state.key_w ? 1 : 0,
                input_state.key_a ? 1 : 0,
                input_state.key_s ? 1 : 0,
                input_state.key_d ? 1 : 0,
                input_state.move.x,
                input_state.move.y,
                input_state.look_delta.x,
                input_state.look_delta.y,
                local_player.camera_rig.yaw,
                local_player.camera_rig.pitch,
                movement_debug.forward.x,
                movement_debug.forward.y,
                movement_debug.forward.z,
                movement_debug.right.x,
                movement_debug.right.y,
                movement_debug.right.z,
                movement_debug.desired.x,
                movement_debug.desired.y,
                movement_debug.desired.z,
                movement_debug.right.x,
                movement_debug.right.y,
                movement_debug.right.z,
                local_player.transform.position.x,
                local_player.transform.position.y,
                local_player.transform.position.z,
                local_player.controller.velocity.x,
                local_player.controller.velocity.y,
                local_player.controller.velocity.z,
                local_player.controller.grounded ? 1 : 0,
                last_collision_debug.penetration_correction,
                last_collision_debug.contact_normal.x,
                last_collision_debug.contact_normal.y,
                last_collision_debug.contact_normal.z,
                input_state.rmb_down ? 1 : 0,
                input_state.pointer_locked ? 1 : 0,
                input_state.look_enabled ? 1 : 0,
                static_cast<int>(remote_players.size()),
                runtime_options.noclip ? 1 : 0);
        }
    }

    refresh_overlay_text();
    rebuild_dynamic_debug_mesh();
    renderer.update_overlay_text(scene.overlay_text);

    RenderFrameContext ctx{};
    ctx.frame_index = frame_index++;
    ctx.alpha = fixed.accumulator / fixed.fixed_dt;
    ctx.delta_seconds = frame_dt;
    ctx.aspect_ratio = 16.0f / 9.0f;

    ctx.view_count = runtime_options.splitscreen ? 2u : 1u;
    ctx.views[0].camera = camera;
    if (runtime_options.splitscreen) {
        ctx.views[0].viewport = glm::vec4(0.0f, 0.0f, 0.5f, 1.0f);
        ctx.views[1].camera = secondary_camera;
        ctx.views[1].viewport = glm::vec4(0.5f, 0.0f, 0.5f, 1.0f);
    } else {
        ctx.views[0].viewport = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
    }
    renderer.begin_frame(ctx, render_stats);
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

void Engine::update_third_person_camera(PlayerEntity &player, Camera &out_camera) {
    const glm::vec3 pivot = player.transform.position + glm::vec3(0.0f, player.camera_rig.pivotHeight, 0.0f);

    const glm::vec3 orbit_forward = PlayerControllerSystem::orbit_forward_from_angles(
        player.camera_rig.yaw,
        player.camera_rig.pitch);

    float camera_distance = player.camera_rig.distance;
    float hit_distance = 0.0f;
    if (collision_world.raycast(pivot, -orbit_forward, player.camera_rig.distance, hit_distance)) {
        camera_distance = std::max(player.camera_rig.minDistance, hit_distance - 0.15f);
    }

    const glm::vec3 camera_pos = pivot - orbit_forward * camera_distance;
    const glm::vec3 view_dir = glm::normalize(pivot - camera_pos);

    out_camera.transform.position = camera_pos;
    // Camera basis uses local -Z as forward at zero rotation, so solve yaw from -view_dir.
    out_camera.transform.euler_radians.y = std::atan2(-view_dir.x, -view_dir.z);
    out_camera.transform.euler_radians.x = std::asin(std::clamp(view_dir.y, -1.0f, 1.0f));
    out_camera.transform.euler_radians.z = 0.0f;
}

void Engine::refresh_overlay_text() {
    scene.overlay_text = RenderMesh{};

    if (runtime_options.devhud) {
        char text[320]{};
        std::snprintf(
            text,
            sizeof(text),
            "FPS %.1f DT %.3f FIX %.3f\nP %.1f %.1f %.1f V %.1f %.1f %.1f G %d\nPEN %.3f N %.1f %.1f %.1f\nYAW %.1f PIT %.1f LOOK %.1f %.1f\nRMB %d LOCK %d LKEN %d REM %d",
            render_stats.fps,
            last_frame_dt,
            fixed.fixed_dt,
            local_player.transform.position.x,
            local_player.transform.position.y,
            local_player.transform.position.z,
            local_player.controller.velocity.x,
            local_player.controller.velocity.y,
            local_player.controller.velocity.z,
            local_player.controller.grounded ? 1 : 0,
            last_collision_debug.penetration_correction,
            last_collision_debug.contact_normal.x,
            last_collision_debug.contact_normal.y,
            last_collision_debug.contact_normal.z,
            local_player.camera_rig.yaw,
            local_player.camera_rig.pitch,
            input_state.look_delta.x,
            input_state.look_delta.y,
            input_state.rmb_down ? 1 : 0,
            input_state.pointer_locked ? 1 : 0,
            input_state.look_enabled ? 1 : 0,
            static_cast<int>(remote_players.size()));
        scene.overlay_text = build_camera_text_mesh(camera, text);
    }

    const std::string menu_text = gui_menu.build_text(runtime_options.devhud, runtime_options.noclip);
    if (!menu_text.empty()) {
        RenderMesh menu_mesh = build_camera_text_mesh(camera, menu_text);
        append_mesh(scene.overlay_text, menu_mesh);
    }
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

    if (runtime_options.splitscreen) {
        RenderMesh p2_capsule = build_debug_capsule_mesh(
            local_player_secondary.transform.position,
            local_player_secondary.controller.capsuleRadius,
            local_player_secondary.controller.capsuleHeight,
            glm::vec3(0.35f, 0.55f, 0.95f));
        RenderMesh p2_target = build_debug_sphere_mesh(
            local_player_secondary.transform.position + glm::vec3(0.0f, local_player_secondary.camera_rig.pivotHeight, 0.0f),
            0.10f,
            glm::vec3(0.6f, 0.85f, 1.0f));
        append_mesh(scene.overlay_text, p2_capsule);
        append_mesh(scene.overlay_text, p2_target);
    }

    if (runtime_options.devhud) {
        for (const glm::ivec3 &cell : last_collision_debug.overlapped_voxels) {
            const glm::vec3 bmin(static_cast<float>(cell.x), static_cast<float>(cell.y), static_cast<float>(cell.z));
            const glm::vec3 bmax = bmin + glm::vec3(1.0f);
            RenderMesh overlap_box = build_debug_aabb_mesh(bmin, bmax, glm::vec3(0.95f, 0.15f, 0.15f));
            append_mesh(scene.overlay_text, overlap_box);
        }

        RenderMesh ground_ray = build_debug_line_mesh(
            last_collision_debug.grounding_ray_origin,
            last_collision_debug.grounding_ray_hit,
            0.01f,
            glm::vec3(1.0f, 1.0f, 0.2f));
        append_mesh(scene.overlay_text, ground_ray);
    }

    for (const auto &[player_id, state] : remote_players) {
        (void)player_id;
        RenderMesh remote_capsule = build_debug_capsule_mesh(
            glm::vec3(state.x, state.y, state.z),
            local_player.controller.capsuleRadius,
            local_player.controller.capsuleHeight,
            glm::vec3(0.3f, 0.8f, 0.35f));
        append_mesh(scene.overlay_text, remote_capsule);
    }
}
