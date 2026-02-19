#include "engine/engine.hpp"

#include "engine_gameplay/animation/skeletal_animator.hpp"
#include "engine_gameplay/player/player_controller.hpp"
#include "engine_render/debug_draw/debug_draw.hpp"
#include "engine_render/debug_text.hpp"

#include <spdlog/spdlog.h>

#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_set>

namespace {
constexpr float k_vehicle_body_half_length = 1.35f;
constexpr float k_vehicle_body_half_width = 0.8f;
constexpr float k_vehicle_body_height = 0.65f;
constexpr float k_vehicle_wheel_radius = 0.32f;
constexpr float k_vehicle_interact_radius = 2.1f;

std::vector<std::string> candidate_model_paths(const char *subdir, const char *model_filename) {
    namespace fs = std::filesystem;
    std::vector<std::string> out;
    out.reserve(8);
    if (!subdir || *subdir == '\0' || !model_filename || *model_filename == '\0') {
        return out;
    }
    const std::string rel = std::string("assets/models/") + subdir + "/" + model_filename;

    // Search from current working directory and a few parent levels so
    // desktop launches from build trees can still resolve repo assets.
    fs::path prefix(".");
    for (int i = 0; i < 6; ++i) {
        fs::path candidate = prefix / rel;
        out.push_back(candidate.generic_string());
        prefix /= "..";
    }

    return out;
}

glm::vec3 rotate_y(const glm::vec3 &v, float yaw_radians) {
    const float c = std::cos(yaw_radians);
    const float s = std::sin(yaw_radians);
    return glm::vec3(v.x * c - v.z * s, v.y, v.x * s + v.z * c);
}

glm::vec3 player_color_from_id(uint32_t player_id) {
    const uint32_t h = (player_id * 2654435761u) ^ 0x9e3779b9u;
    const float r = 0.25f + 0.65f * static_cast<float>((h >> 0) & 0xFF) / 255.0f;
    const float g = 0.25f + 0.65f * static_cast<float>((h >> 8) & 0xFF) / 255.0f;
    const float b = 0.25f + 0.65f * static_cast<float>((h >> 16) & 0xFF) / 255.0f;
    return glm::vec3(r, g, b);
}

const char *anim_state_name(PlayerAnimState state) {
    switch (state) {
    case PlayerAnimState::Idle:
        return "IDLE";
    case PlayerAnimState::Walk:
        return "WALK";
    case PlayerAnimState::Run:
        return "RUN";
    case PlayerAnimState::Jump:
        return "JUMP";
    case PlayerAnimState::Crawl:
        return "CRAWL";
    default:
        return "UNK";
    }
}

struct AnimatedCapsuleShape {
    float radius = 0.35f;
    float height = 1.8f;
    float bob = 0.0f;
    float pivot_height = 1.5f;
};

float anim_pulse(float phase) {
    return std::fabs(std::sin(phase));
}

glm::quat facing_from_velocity(glm::vec3 velocity, const glm::quat &fallback) {
    const glm::vec2 flat(velocity.x, velocity.z);
    const float speed = glm::length(flat);
    if (speed < 0.08f) {
        return fallback;
    }
    const float yaw = std::atan2(velocity.x, velocity.z);
    return glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f));
}

RenderMesh build_wireframe_from_mesh(const RenderMesh &mesh, float thickness, const glm::vec3 &color) {
    RenderMesh out{};
    if (mesh.indices.size() < 3 || mesh.vertices.empty()) {
        return out;
    }

    std::unordered_set<uint64_t> edges;
    edges.reserve(mesh.indices.size());
    auto add_edge = [&](uint32_t a, uint32_t b) {
        const uint32_t lo = std::min(a, b);
        const uint32_t hi = std::max(a, b);
        const uint64_t key = (static_cast<uint64_t>(lo) << 32u) | static_cast<uint64_t>(hi);
        if (!edges.insert(key).second) {
            return;
        }
        if (lo >= mesh.vertices.size() || hi >= mesh.vertices.size()) {
            return;
        }
        append_mesh(out, build_debug_line_mesh(mesh.vertices[lo].position, mesh.vertices[hi].position, thickness, color));
    };

    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const uint32_t a = mesh.indices[i + 0];
        const uint32_t b = mesh.indices[i + 1];
        const uint32_t c = mesh.indices[i + 2];
        add_edge(a, b);
        add_edge(b, c);
        add_edge(c, a);
    }
    return out;
}

AnimatedCapsuleShape animated_shape(
    uint8_t anim_state,
    float anim_phase,
    float anim_blend,
    float base_radius,
    float base_height,
    float base_pivot_height) {
    AnimatedCapsuleShape out{};
    out.radius = base_radius;
    out.height = base_height;
    out.pivot_height = base_pivot_height;
    out.bob = 0.01f * std::sin(anim_phase);

    switch (anim_state) {
    case static_cast<uint8_t>(PlayerAnimState::Walk):
        out.bob = 0.06f * std::max(0.35f, anim_blend) * anim_pulse(anim_phase);
        break;
    case static_cast<uint8_t>(PlayerAnimState::Run):
        out.bob = 0.11f * std::max(0.55f, anim_blend) * anim_pulse(anim_phase);
        break;
    case static_cast<uint8_t>(PlayerAnimState::Jump):
        out.bob = 0.08f * std::sin(anim_phase * 0.65f);
        break;
    case static_cast<uint8_t>(PlayerAnimState::Crawl):
        out.height = base_height * 0.55f;
        out.radius = base_radius * 1.08f;
        out.pivot_height = base_pivot_height * 0.62f;
        out.bob = 0.02f * anim_pulse(anim_phase * 0.8f);
        break;
    case static_cast<uint8_t>(PlayerAnimState::Idle):
    default:
        break;
    }

    return out;
}

bool try_load_character_model(
    SkinnedModel &model,
    const char *label,
    const char *filename,
    bool &out_loaded) {
    std::string load_error;
    for (const std::string &path : candidate_model_paths("player", filename)) {
        if (model.load_from_glb(path, load_error)) {
            out_loaded = true;
            spdlog::info("Loaded {} player model from {}", label, path);
            return true;
        }
    }
    spdlog::warn("{} player model not loaded: {}", label, load_error);
    return false;
}

void disable_gameplay_actions(InputState &input) {
    input.move = glm::vec2(0.0f);
    input.jump_pressed = false;
    input.jump_held = false;
    input.interact_pressed = false;
    input.sprint_held = false;
    input.crouch_held = false;
}
}

void Engine::init(void *window_handle, RenderBackendType backend_type, const EngineRuntimeOptions &options) {
    runtime_options = options;

    EnginePhysicsSettings settings{};
    physics.init(settings);
    net_client.init();
    ui_audio.init();

    build_static_scene();
    local_player = PlayerControllerSystem::spawn_player(collision_world);
    local_player_prev_position = local_player.transform.position;
    vehicle.position = local_player.transform.position + glm::vec3(3.5f, 0.0f, 1.5f);
    vehicle.yaw = 0.3f;
    vehicle.speed = 0.0f;
    vehicle.occupied = false;
    vehicle.position.y = collision_world.find_spawn_height(
        glm::vec2(vehicle.position.x, vehicle.position.z),
        0.8f,
        1.2f) +
        k_vehicle_wheel_radius;
    if (runtime_options.splitscreen) {
        local_player_secondary = PlayerControllerSystem::spawn_player(collision_world);
        local_player_secondary.network_id = 2;
        local_player_secondary.transform.position.x += 2.5f;
        local_player_secondary.camera_rig.yaw = 180.0f;
        local_player_secondary_prev_position = local_player_secondary.transform.position;
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

    try_load_character_model(fox_player_model, "fox", "Fox.glb", has_fox_player_model);
    try_load_character_model(humanoid_player_model, "humanoid", "CesiumMan.glb", has_humanoid_player_model);

    try {
        renderer.init(window_handle, backend_type);
        renderer.upload_scene(scene);
        renderer.update_dynamic_meshes(scene.debug_world, scene.debug_screen);
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

void Engine::start_local_server(uint16_t port, bool loopback_only) {
    if (local_server_running && local_server_loopback == loopback_only) {
        return;
    }
    if (local_server_running) {
        local_server.shutdown();
        local_server_running = false;
    }
    local_server.init(port, loopback_only);
    local_server_loopback = loopback_only;
    local_server_running = true;
    spdlog::info("Started {} server on {}", loopback_only ? "local-only" : "LAN", port);
}

void Engine::set_input(const InputState &input_primary, const InputState &input_secondary, bool touch_mode) {
    input_state = input_primary;
    input_state_secondary = input_secondary;
    touch_input_mode = touch_mode;
}

void Engine::shutdown() {
    lan_discovery.stop();
    if (local_server_running) {
        local_server.shutdown();
        local_server_running = false;
    }
    renderer.shutdown();
    ui_audio.shutdown();
    net_client.disconnect();
    net_client.shutdown();
    physics.shutdown();
}

void Engine::sync_network_state(uint32_t sim_tick, const InputState &net_input) {
    NetTickInput input{};
    input.tick = sim_tick;
    input.move_x = net_input.move.x;
    input.move_y = net_input.move.y;
    if (net_input.jump_held) {
        input.action_flags |= net_flag(NetInputFlags::JumpHeld);
    }
    if (net_input.jump_pressed) {
        input.action_flags |= net_flag(NetInputFlags::JumpPressed);
    }
    if (net_input.sprint_held) {
        input.action_flags |= net_flag(NetInputFlags::SprintHeld);
    }
    if (net_input.crouch_held) {
        input.action_flags |= net_flag(NetInputFlags::CrouchHeld);
    }
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
    std::unordered_set<uint32_t> seen_remote_ids;
    for (const auto &[player_id, state] : net_client.player_states()) {
        if (player_id == local_player.network_id) {
            continue;
        }
        remote_players[player_id] = state;

        RemoteRenderPlayer &render_player = remote_render_players[player_id];
        glm::vec3 target = glm::vec3(state.x, state.y, state.z);
        if (!std::isfinite(target.x) || !std::isfinite(target.y) || !std::isfinite(target.z)) {
            target = glm::vec3(state.x, local_player.transform.position.y, state.z);
        }
        if (!render_player.initialized) {
            render_player.position = target;
            render_player.orientation = local_player.transform.rotation;
            render_player.target_orientation = render_player.orientation;
            render_player.initialized = true;
        }
        render_player.target_position = target;
        render_player.velocity = glm::vec3(state.vx, state.vy, state.vz);
        render_player.target_orientation = facing_from_velocity(render_player.velocity, render_player.target_orientation);
        render_player.anim_state = state.anim_state;
        render_player.anim_phase = state.anim_phase;
        render_player.anim_blend = state.anim_blend;
        seen_remote_ids.insert(player_id);
    }

    for (auto it = remote_render_players.begin(); it != remote_render_players.end();) {
        if (seen_remote_ids.find(it->first) == seen_remote_ids.end()) {
            it = remote_render_players.erase(it);
        } else {
            ++it;
        }
    }
}

void Engine::tick(double frame_dt) {
    last_frame_dt = frame_dt;

    if (input_state.debug_toggle_pressed) {
        runtime_options.debug_collision = !runtime_options.debug_collision;
    }
    if (input_state.debug_xray_toggle_pressed) {
        runtime_options.debug_xray = !runtime_options.debug_xray;
    }
    if (input_state.debug_collision_only_toggle_pressed) {
        runtime_options.debug_collision_only = !runtime_options.debug_collision_only;
    }
    if (input_state.debug_freeze_toggle_pressed) {
        runtime_options.debug_freeze = !runtime_options.debug_freeze;
        if (!runtime_options.debug_freeze) {
            frozen_debug_world = RenderMesh{};
        }
    }

    GuiMenuActions menu_actions{};
    gui_menu.handle_input(input_state, runtime_options.devhud, runtime_options.noclip, menu_actions);
    if (menu_actions.ui_move_sfx) {
        ui_audio.play_move();
    }
    if (menu_actions.ui_select_sfx) {
        ui_audio.play_click();
    }
    if (menu_actions.start_game) {
        gameplay_started = true;
    }
    if (menu_actions.close_menu && !gameplay_started) {
        gameplay_started = true;
    }
    if (menu_actions.host_local) {
        gameplay_started = true;
        start_local_server(7777, true);
        connect("127.0.0.1", 7777);
        lan_discovery.stop();
        searching_nearby = false;
        multiplayer_hint = "Hosting this device only.";
    }
    if (menu_actions.host_lan) {
        gameplay_started = true;
        start_local_server(7777, false);
        connect("127.0.0.1", 7777);
        lan_discovery.start_host(7777, "VOXOV Host");
        searching_nearby = false;
        multiplayer_hint = "Hosting Wi-Fi game. Tell friends: Multiplayer > Join Nearby.";
    }
    if (menu_actions.join_local || menu_actions.join_nearby) {
        lan_discovery.start_client();
        searching_nearby = true;
        multiplayer_hint = "Searching nearby Wi-Fi hosts...";
    }
    if (local_server_running && !local_server_loopback) {
        lan_discovery.pump();
    }
    if (searching_nearby && !net_client.is_connected()) {
        lan_discovery.pump();
        LanHostEntry host{};
        if (lan_discovery.pop_host(host)) {
            connect(host.ip.c_str(), host.port);
            searching_nearby = false;
            multiplayer_hint = "Joining " + host.name + " (" + host.ip + ")";
        }
    } else if (searching_nearby && net_client.is_connected()) {
        searching_nearby = false;
    }
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
        disable_gameplay_actions(gameplay_input);
        gameplay_input.look_delta = glm::vec2(0.0f);
    }
    InputState gameplay_input_secondary = input_state_secondary;
    if (gui_menu.open()) {
        disable_gameplay_actions(gameplay_input_secondary);
        gameplay_input_secondary.look_delta = glm::vec2(0.0f);
    }
    if (!gameplay_started) {
        disable_gameplay_actions(gameplay_input);
        gameplay_input_secondary = gameplay_input;
    }

    handle_vehicle_interaction(gameplay_input);

    PlayerControllerSystem::update_camera_rig(local_player, gameplay_input, touch_input_mode, static_cast<float>(frame_dt));
    if (runtime_options.splitscreen) {
        PlayerControllerSystem::update_camera_rig(local_player_secondary, gameplay_input_secondary, false, static_cast<float>(frame_dt));
    }

    fixed.accumulator += frame_dt;
    bool jump_consumed = false;

    while (fixed.accumulator >= fixed.fixed_dt) {
        if (local_server_running) {
            local_server.pump();
        }
        local_player_prev_position = local_player.transform.position;
        InputState step_input = gameplay_input;
        if (jump_consumed) {
            step_input.jump_pressed = false;
        }
        update_vehicle_sim(step_input, static_cast<float>(fixed.fixed_dt));
        if (vehicle.occupied) {
            last_collision_debug = PlayerCollisionDebug{};
        } else {
            last_collision_debug = PlayerControllerSystem::simulate_fixed(
                local_player,
                step_input,
                collision_world,
                static_cast<float>(fixed.fixed_dt),
                runtime_options.noclip);
        }

        if (runtime_options.splitscreen) {
            local_player_secondary_prev_position = local_player_secondary.transform.position;
            InputState step_input_secondary = gameplay_input_secondary;
            last_collision_debug_secondary = PlayerControllerSystem::simulate_fixed(
                local_player_secondary,
                step_input_secondary,
                collision_world,
                static_cast<float>(fixed.fixed_dt),
                runtime_options.noclip);
        }
        jump_consumed = jump_consumed || input_state.jump_pressed;

        sync_network_state(static_cast<uint32_t>(fixed.tick), step_input);

    local_replication.position = local_player.transform.position;
    local_replication.velocity = local_player.controller.velocity;

        physics.step(static_cast<float>(fixed.fixed_dt));
        fixed.accumulator -= fixed.fixed_dt;
        fixed.tick++;
    }

    input_state.jump_pressed = false;
    input_state.interact_pressed = false;

    const float alpha = static_cast<float>(std::clamp(fixed.accumulator / fixed.fixed_dt, 0.0, 1.0));
    const glm::vec3 local_player_render_position = glm::mix(local_player_prev_position, local_player.transform.position, alpha);
    update_third_person_camera(local_player, local_player_render_position, camera);
    if (runtime_options.splitscreen) {
        const glm::vec3 secondary_render_position = glm::mix(local_player_secondary_prev_position, local_player_secondary.transform.position, alpha);
        update_third_person_camera(local_player_secondary, secondary_render_position, secondary_camera);
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
        if (any_non_zero && log_accumulator >= 0.2) {
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

    const float remote_lerp = std::clamp(static_cast<float>(frame_dt) * 12.0f, 0.0f, 1.0f);
    const float remote_rot_lerp = std::clamp(static_cast<float>(frame_dt) * 9.0f, 0.0f, 1.0f);
    for (auto &[player_id, render_player] : remote_render_players) {
        (void)player_id;
        const glm::vec3 predicted_target = render_player.target_position + render_player.velocity * 0.035f;
        const float err = glm::length(render_player.position - predicted_target);
        if (err > 4.0f) {
            render_player.position = predicted_target;
        } else {
            render_player.position = glm::mix(render_player.position, predicted_target, remote_lerp);
        }
        render_player.orientation = glm::normalize(glm::slerp(
            render_player.orientation,
            render_player.target_orientation,
            remote_rot_lerp));
    }

    render_stats.net_connected = net_client.is_connected();
    render_stats.net_local_player_id = local_player.network_id;
    render_stats.net_remote_count = static_cast<uint32_t>(remote_render_players.size());

    refresh_overlay_text();
    if (!runtime_options.debug_freeze || frozen_debug_world.vertices.empty()) {
        rebuild_dynamic_debug_mesh();
        if (runtime_options.debug_freeze) {
            frozen_debug_world = scene.debug_world;
        }
    } else {
        scene.debug_world = frozen_debug_world;
    }
    renderer.update_dynamic_meshes(scene.debug_world, scene.debug_screen);

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
    ctx.debug_xray = runtime_options.debug_collision && runtime_options.debug_xray;
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

    vehicle.position = glm::vec3(12.0f, 0.0f, 12.0f);
    vehicle.yaw = 0.0f;
    vehicle.speed = 0.0f;
    vehicle.occupied = false;
    const float ground_y = collision_world.find_spawn_height(
        glm::vec2(vehicle.position.x, vehicle.position.z),
        0.8f,
        1.2f);
    vehicle.position.y = ground_y + k_vehicle_wheel_radius;
}

void Engine::update_third_person_camera(PlayerEntity &player, Camera &out_camera) {
    update_third_person_camera(player, player.transform.position, out_camera);
}

void Engine::update_third_person_camera(PlayerEntity &player, const glm::vec3 &render_position, Camera &out_camera) {
    const glm::vec3 pivot = render_position + glm::vec3(0.0f, player.camera_rig.pivotHeight, 0.0f);

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

glm::vec3 Engine::vehicle_seat_world_position() const {
    return vehicle.position + rotate_y(glm::vec3(0.0f, k_vehicle_body_height + 0.5f, 0.0f), vehicle.yaw);
}

void Engine::handle_vehicle_interaction(const InputState &input) {
    if (!input.interact_pressed || gui_menu.open() || !gameplay_started) {
        return;
    }

    if (vehicle.occupied) {
        vehicle.occupied = false;
        const glm::vec3 exit_candidate = vehicle.position + rotate_y(glm::vec3(-1.8f, 0.0f, 0.0f), vehicle.yaw);
        local_player.transform.position = exit_candidate;
        local_player.transform.position.y = collision_world.find_spawn_height(
            glm::vec2(local_player.transform.position.x, local_player.transform.position.z),
            local_player.controller.capsuleRadius,
            local_player.controller.capsuleHeight) +
            0.05f;
        local_player.controller.velocity = glm::vec3(0.0f);
        local_player.controller.grounded = true;
        return;
    }

    const float d = glm::length(local_player.transform.position - vehicle.position);
    if (d <= k_vehicle_interact_radius) {
        vehicle.occupied = true;
        local_player.transform.position = vehicle_seat_world_position();
        local_player.controller.velocity = glm::vec3(0.0f);
        local_player.controller.grounded = true;
    }
}

void Engine::update_vehicle_sim(const InputState &input, float dt) {
    if (!vehicle.occupied) {
        vehicle.speed *= std::exp(-dt * 3.5f);
        if (std::fabs(vehicle.speed) < 0.02f) {
            vehicle.speed = 0.0f;
        }
        return;
    }

    const float throttle = input.move.y;
    const float steer = input.move.x;
    constexpr float k_accel = 14.0f;
    constexpr float k_brake = 12.0f;
    constexpr float k_max_speed = 17.0f;
    constexpr float k_reverse_speed = 6.0f;
    constexpr float k_steer_rate = 1.7f;

    if (std::fabs(throttle) > 0.05f) {
        vehicle.speed += throttle * k_accel * dt;
    } else {
        vehicle.speed -= vehicle.speed * std::min(1.0f, k_brake * dt);
    }
    vehicle.speed = std::clamp(vehicle.speed, -k_reverse_speed, k_max_speed);

    const float steer_amount = steer * std::clamp(std::fabs(vehicle.speed) / k_max_speed, 0.2f, 1.0f);
    vehicle.yaw += steer_amount * k_steer_rate * dt * (vehicle.speed >= 0.0f ? 1.0f : -1.0f);

    const glm::vec3 fwd = rotate_y(glm::vec3(0.0f, 0.0f, 1.0f), vehicle.yaw);
    vehicle.position += fwd * (vehicle.speed * dt);

    const glm::vec2 drive_center(12.0f, 12.0f);
    constexpr float k_drive_half_extent = 22.0f;
    vehicle.position.x = std::clamp(vehicle.position.x, drive_center.x - k_drive_half_extent, drive_center.x + k_drive_half_extent);
    vehicle.position.z = std::clamp(vehicle.position.z, drive_center.y - k_drive_half_extent, drive_center.y + k_drive_half_extent);

    const float ground_y = collision_world.find_spawn_height(glm::vec2(vehicle.position.x, vehicle.position.z), 0.9f, 1.4f);
    vehicle.position.y = ground_y + k_vehicle_wheel_radius;

    local_player.transform.position = vehicle_seat_world_position();
    local_player.transform.rotation = glm::angleAxis(vehicle.yaw, glm::vec3(0.0f, 1.0f, 0.0f));
    local_player.controller.velocity = glm::vec3(0.0f);
    local_player.controller.grounded = true;
    local_player.anim_state = PlayerAnimState::Idle;
    local_player.anim_blend = 0.0f;
}

void Engine::refresh_overlay_text() {
    scene.debug_screen = RenderMesh{};

    if (runtime_options.devhud) {
        char text[320]{};
        const float vehicle_distance = glm::length(local_player.transform.position - vehicle.position);
        std::snprintf(
            text,
            sizeof(text),
            "FPS %.1f DT %.3f FIX %.3f\nP %.1f %.1f %.1f V %.1f %.1f %.1f G %d\nPEN %.3f N %.1f %.1f %.1f\nYAW %.1f PIT %.1f LOOK %.1f %.1f\nRMB %d LOCK %d LKEN %d REM %d\nNET C%d LID %u\nANIM %s BL %.2f PH %.2f\nVEH %s DIST %.1f (F TO ENTER/EXIT)",
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
            static_cast<int>(remote_render_players.size()),
            render_stats.net_connected ? 1 : 0,
            render_stats.net_local_player_id,
            anim_state_name(local_player.anim_state),
            local_player.anim_blend,
            local_player.anim_phase,
            vehicle.occupied ? "ONBOARD" : "ON FOOT",
            vehicle_distance);
        scene.debug_screen = build_camera_text_mesh(camera, text);
    }

    if (net_client.is_connected()) {
        multiplayer_hint = "Connected to game server.";
    }
    const GuiMenuView menu_view = gui_menu.build_view(runtime_options.devhud, runtime_options.noclip, multiplayer_hint);
    render_stats.menu_open = menu_view.open;
    render_stats.menu_selected = menu_view.selected;
    render_stats.menu_title = menu_view.title;
    render_stats.menu_items = menu_view.items;
    render_stats.menu_guide = menu_view.guide_lines;
    render_stats.menu_status = menu_view.status;
    const std::string menu_text = gui_menu.build_text(runtime_options.devhud, runtime_options.noclip, multiplayer_hint);
    render_stats.menu_text = menu_text;
}

void Engine::rebuild_dynamic_debug_mesh() {
    scene.debug_world = RenderMesh{};
    const bool collision_debug_enabled = runtime_options.debug_collision;
    const SkinnedModel *selected_player_model = nullptr;
    if (gui_menu.character() == GuiMenu::Character::Fox && has_fox_player_model) {
        selected_player_model = &fox_player_model;
    } else if (gui_menu.character() == GuiMenu::Character::Humanoid && has_humanoid_player_model) {
        selected_player_model = &humanoid_player_model;
    }
    const bool render_skinned_avatar = selected_player_model != nullptr;
    const bool render_fox_wireframe = gui_menu.character() == GuiMenu::Character::Fox;

    const AnimatedCapsuleShape local_shape = animated_shape(
        static_cast<uint8_t>(local_player.anim_state),
        local_player.anim_phase,
        local_player.anim_blend,
        local_player.controller.capsuleRadius,
        local_player.controller.capsuleHeight,
        local_player.camera_rig.pivotHeight);

    RenderMesh player_capsule = build_debug_capsule_mesh(
        local_player.transform.position + glm::vec3(0.0f, local_shape.bob, 0.0f),
        local_shape.radius,
        local_shape.height,
        player_color_from_id(local_player.network_id));

    RenderMesh target_marker = build_debug_sphere_mesh(
        local_player.transform.position + glm::vec3(0.0f, local_shape.pivot_height + local_shape.bob, 0.0f),
        0.12f,
        glm::vec3(0.2f, 0.85f, 1.0f));

    auto append_vehicle_tri = [&](const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c, const glm::vec3 &color) {
        const uint32_t base = static_cast<uint32_t>(scene.debug_world.vertices.size());
        scene.debug_world.vertices.push_back({a, color});
        scene.debug_world.vertices.push_back({b, color});
        scene.debug_world.vertices.push_back({c, color});
        scene.debug_world.indices.push_back(base + 0);
        scene.debug_world.indices.push_back(base + 1);
        scene.debug_world.indices.push_back(base + 2);
    };
    auto append_vehicle_quad =
        [&](const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c, const glm::vec3 &d, const glm::vec3 &color) {
            append_vehicle_tri(a, b, c, color);
            append_vehicle_tri(a, c, d, color);
        };
    auto append_vehicle_box = [&](const glm::vec3 &local_center, const glm::vec3 &half_extent, const glm::vec3 &color) {
        const glm::vec3 lc[8] = {
            {-half_extent.x, -half_extent.y, -half_extent.z},
            {half_extent.x, -half_extent.y, -half_extent.z},
            {-half_extent.x, half_extent.y, -half_extent.z},
            {half_extent.x, half_extent.y, -half_extent.z},
            {-half_extent.x, -half_extent.y, half_extent.z},
            {half_extent.x, -half_extent.y, half_extent.z},
            {-half_extent.x, half_extent.y, half_extent.z},
            {half_extent.x, half_extent.y, half_extent.z},
        };
        glm::vec3 p[8]{};
        for (int i = 0; i < 8; ++i) {
            p[i] = vehicle.position + rotate_y(local_center + lc[i], vehicle.yaw);
        }
        append_vehicle_quad(p[0], p[1], p[3], p[2], color);
        append_vehicle_quad(p[4], p[6], p[7], p[5], color);
        append_vehicle_quad(p[0], p[2], p[6], p[4], color);
        append_vehicle_quad(p[1], p[5], p[7], p[3], color);
        append_vehicle_quad(p[2], p[3], p[7], p[6], color);
        append_vehicle_quad(p[0], p[4], p[5], p[1], color);
    };

    if (!runtime_options.debug_collision_only) {
        append_vehicle_box(
            glm::vec3(0.0f, k_vehicle_wheel_radius + k_vehicle_body_height * 0.5f, 0.0f),
            glm::vec3(k_vehicle_body_half_width, k_vehicle_body_height * 0.5f, k_vehicle_body_half_length),
            vehicle.occupied ? glm::vec3(0.15f, 0.78f, 0.35f) : glm::vec3(0.85f, 0.62f, 0.22f));
        append_vehicle_box(
            glm::vec3(0.0f, k_vehicle_wheel_radius + k_vehicle_body_height + 0.28f, -0.1f),
            glm::vec3(0.58f, 0.28f, 0.68f),
            glm::vec3(0.2f, 0.35f, 0.42f));
        append_vehicle_box(
            glm::vec3(0.0f, k_vehicle_wheel_radius + 0.58f, k_vehicle_body_half_length - 0.22f),
            glm::vec3(0.55f, 0.12f, 0.14f),
            glm::vec3(0.08f, 0.08f, 0.08f));

        const glm::vec3 wheel_offsets[4] = {
            {-k_vehicle_body_half_width - 0.1f, k_vehicle_wheel_radius, -k_vehicle_body_half_length + 0.28f},
            {k_vehicle_body_half_width + 0.1f, k_vehicle_wheel_radius, -k_vehicle_body_half_length + 0.28f},
            {-k_vehicle_body_half_width - 0.1f, k_vehicle_wheel_radius, k_vehicle_body_half_length - 0.28f},
            {k_vehicle_body_half_width + 0.1f, k_vehicle_wheel_radius, k_vehicle_body_half_length - 0.28f},
        };
        for (const glm::vec3 &off : wheel_offsets) {
            append_mesh(
                scene.debug_world,
                build_debug_sphere_mesh(vehicle.position + rotate_y(off, vehicle.yaw), k_vehicle_wheel_radius, glm::vec3(0.12f, 0.12f, 0.12f)));
        }

        append_mesh(
            scene.debug_world,
            build_debug_line_mesh(
                vehicle.position + glm::vec3(0.0f, 0.2f, 0.0f),
                vehicle.position + glm::vec3(0.0f, 4.2f, 0.0f),
                0.06f,
                glm::vec3(1.0f, 0.25f, 0.9f)));
        append_mesh(
            scene.debug_world,
            build_debug_sphere_mesh(vehicle.position + glm::vec3(0.0f, 4.35f, 0.0f), 0.22f, glm::vec3(1.0f, 0.25f, 0.9f)));
    }

    if (!runtime_options.debug_collision_only) {
        if (!render_skinned_avatar || collision_debug_enabled || runtime_options.devhud) {
            append_mesh(scene.debug_world, player_capsule);
            append_mesh(scene.debug_world, target_marker);
        }
        if (render_skinned_avatar) {
            const RenderMesh local_model = selected_player_model->build_render_mesh(
                local_player.anim_state,
                local_player.anim_phase,
                local_player.anim_blend,
                local_player.transform.position + glm::vec3(0.0f, local_shape.bob, 0.0f),
                local_player.transform.rotation,
                player_color_from_id(local_player.network_id));
            if (render_fox_wireframe) {
                append_mesh(scene.debug_world, build_wireframe_from_mesh(local_model, 0.01f, glm::vec3(0.9f, 0.95f, 1.0f)));
            } else {
                append_mesh(scene.debug_world, local_model);
            }
        }
        if (render_skinned_avatar && (collision_debug_enabled || runtime_options.devhud)) {
            const SkeletonPose local_pose = SkeletalAnimator::sample_pose(
                local_player.anim_state,
                local_player.anim_phase,
                local_player.anim_blend);
            SkeletalAnimator::append_debug_skeleton(
                scene.debug_world,
                local_pose,
                local_player.transform.position + glm::vec3(0.0f, local_shape.bob, 0.0f),
                local_player.transform.rotation,
                glm::vec3(0.95f, 0.97f, 1.0f),
                0.012f);
        }
    }

    if (runtime_options.splitscreen) {
        const AnimatedCapsuleShape p2_shape = animated_shape(
            static_cast<uint8_t>(local_player_secondary.anim_state),
            local_player_secondary.anim_phase,
            local_player_secondary.anim_blend,
            local_player_secondary.controller.capsuleRadius,
            local_player_secondary.controller.capsuleHeight,
            local_player_secondary.camera_rig.pivotHeight);
        RenderMesh p2_capsule = build_debug_capsule_mesh(
            local_player_secondary.transform.position + glm::vec3(0.0f, p2_shape.bob, 0.0f),
            p2_shape.radius,
            p2_shape.height,
            player_color_from_id(local_player_secondary.network_id));
        RenderMesh p2_target = build_debug_sphere_mesh(
            local_player_secondary.transform.position + glm::vec3(0.0f, p2_shape.pivot_height + p2_shape.bob, 0.0f),
            0.10f,
            glm::vec3(0.6f, 0.85f, 1.0f));
        if (!runtime_options.debug_collision_only) {
            if (!render_skinned_avatar || collision_debug_enabled || runtime_options.devhud) {
                append_mesh(scene.debug_world, p2_capsule);
                append_mesh(scene.debug_world, p2_target);
            }
            if (render_skinned_avatar) {
                const RenderMesh p2_model = selected_player_model->build_render_mesh(
                    local_player_secondary.anim_state,
                    local_player_secondary.anim_phase,
                    local_player_secondary.anim_blend,
                    local_player_secondary.transform.position + glm::vec3(0.0f, p2_shape.bob, 0.0f),
                    local_player_secondary.transform.rotation,
                    player_color_from_id(local_player_secondary.network_id));
                if (render_fox_wireframe) {
                    append_mesh(scene.debug_world, build_wireframe_from_mesh(p2_model, 0.009f, glm::vec3(0.86f, 0.92f, 1.0f)));
                } else {
                    append_mesh(scene.debug_world, p2_model);
                }
            }
        }
    }

    if (collision_debug_enabled && (runtime_options.devhud || runtime_options.debug_collision_only)) {
        for (const glm::ivec3 &cell : last_collision_debug.overlapped_voxels) {
            const glm::vec3 bmin(static_cast<float>(cell.x), static_cast<float>(cell.y), static_cast<float>(cell.z));
            const glm::vec3 bmax = bmin + glm::vec3(1.0f);
            RenderMesh overlap_box = build_debug_aabb_mesh(bmin, bmax, glm::vec3(0.95f, 0.15f, 0.15f));
            append_mesh(scene.debug_world, overlap_box);
        }

        RenderMesh ground_ray = build_debug_line_mesh(
            last_collision_debug.grounding_ray_origin,
            last_collision_debug.grounding_ray_hit,
            0.01f,
            glm::vec3(1.0f, 1.0f, 0.2f));
        append_mesh(scene.debug_world, ground_ray);

        RenderMesh normal_line = build_debug_line_mesh(
            local_player.transform.position + glm::vec3(0.0f, 0.05f, 0.0f),
            local_player.transform.position + glm::vec3(0.0f, 0.05f, 0.0f) + last_collision_debug.contact_normal * 0.6f,
            0.01f,
            glm::vec3(1.0f, 0.4f, 0.1f));
        append_mesh(scene.debug_world, normal_line);
    }

    for (const auto &[player_id, render_player] : remote_render_players) {
        (void)player_id;
        if (runtime_options.debug_collision_only) {
            continue;
        }
        const float remote_y = std::isfinite(render_player.position.y)
            ? render_player.position.y
            : collision_world.find_spawn_height(
                  glm::vec2(render_player.position.x, render_player.position.z),
                  local_player.controller.capsuleRadius,
                  local_player.controller.capsuleHeight) +
                  0.05f;
        const AnimatedCapsuleShape remote_shape = animated_shape(
            render_player.anim_state,
            render_player.anim_phase,
            render_player.anim_blend,
            local_player.controller.capsuleRadius,
            local_player.controller.capsuleHeight,
            local_player.camera_rig.pivotHeight);
        const glm::vec3 remote_base = glm::vec3(render_player.position.x, remote_y, render_player.position.z);
        if (!render_skinned_avatar || collision_debug_enabled || runtime_options.devhud) {
            RenderMesh remote_capsule = build_debug_capsule_mesh(
                remote_base + glm::vec3(0.0f, remote_shape.bob, 0.0f),
                remote_shape.radius,
                remote_shape.height,
                player_color_from_id(player_id));
            append_mesh(scene.debug_world, remote_capsule);
        }
        if (render_skinned_avatar) {
            const RenderMesh remote_model = selected_player_model->build_render_mesh(
                static_cast<PlayerAnimState>(render_player.anim_state),
                render_player.anim_phase,
                render_player.anim_blend,
                remote_base + glm::vec3(0.0f, remote_shape.bob, 0.0f),
                render_player.orientation,
                player_color_from_id(player_id) * glm::vec3(1.08f, 1.08f, 1.08f));
            if (render_fox_wireframe) {
                append_mesh(scene.debug_world, build_wireframe_from_mesh(remote_model, 0.008f, glm::vec3(0.82f, 0.9f, 1.0f)));
            } else {
                append_mesh(scene.debug_world, remote_model);
            }
        }
    }
}
