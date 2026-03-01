#include "engine/engine.hpp"

#include "engine_gameplay/animation/skeletal_animator.hpp"
#include "engine_gameplay/player/player_controller.hpp"
#include "engine_gameplay/player/player_visuals.hpp"
#include "engine_physics/avbd_solver.hpp"
#include "engine_render/debug_draw/debug_draw.hpp"
#include "engine_render/debug_text.hpp"

#include <spdlog/spdlog.h>

#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_set>
#if defined(_WIN32)
#include <windows.h>
#elif defined(__linux__)
#include <unistd.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace {
constexpr float k_vehicle_body_half_length = 1.35f;
constexpr float k_vehicle_body_half_width = 0.8f;
constexpr float k_vehicle_body_height = 0.65f;
constexpr float k_vehicle_wheel_radius = 0.32f;
constexpr float k_vehicle_interact_radius = 2.1f;
constexpr bool k_vehicle_feature_enabled = true;
constexpr float k_vehicle_visual_yaw_offset = 0.0f;
constexpr float k_aircraft_interact_radius = 4.2f;
constexpr float k_aircraft_body_length = 2.7f;
constexpr float k_aircraft_body_width = 1.1f;
constexpr float k_aircraft_body_height = 0.55f;
constexpr uint32_t k_remote_interp_delay_ticks = 6;
constexpr size_t k_remote_sample_history_max = 16;
constexpr float k_minigame_interact_radius = 4.5f;

std::filesystem::path executable_directory() {
    namespace fs = std::filesystem;
#if defined(_WIN32)
    std::array<char, 4096> path{};
    const unsigned long len = GetModuleFileNameA(nullptr, path.data(), static_cast<unsigned long>(path.size()));
    if (len > 0 && len < path.size()) {
        return fs::path(std::string(path.data(), len)).parent_path();
    }
    return fs::path(".");
#elif defined(__linux__)
    std::array<char, 4096> path{};
    const ssize_t len = readlink("/proc/self/exe", path.data(), path.size() - 1);
    if (len > 0) {
        path[static_cast<size_t>(len)] = '\0';
        return fs::path(path.data()).parent_path();
    }
    return fs::path(".");
#elif defined(__APPLE__)
    std::array<char, 4096> path{};
    uint32_t size = static_cast<uint32_t>(path.size());
    if (_NSGetExecutablePath(path.data(), &size) == 0) {
        return fs::path(path.data()).parent_path();
    }
    return fs::path(".");
#else
    return fs::path(".");
#endif
}

std::vector<std::string> candidate_model_paths(const char *subdir, const char *model_filename) {
    namespace fs = std::filesystem;
    std::vector<std::string> out;
    out.reserve(20);
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

    // Also resolve relative to executable dir for packaged installs.
    fs::path exe_prefix = executable_directory();
    for (int i = 0; i < 6; ++i) {
        fs::path candidate = exe_prefix / rel;
        out.push_back(candidate.generic_string());
        exe_prefix /= "..";
    }

    return out;
}

glm::vec3 rotate_y(const glm::vec3 &v, float yaw_radians) {
    const float c = std::cos(yaw_radians);
    const float s = std::sin(yaw_radians);
    return glm::vec3(v.x * c - v.z * s, v.y, v.x * s + v.z * c);
}

glm::vec3 player_color_from_id(uint32_t player_id) {
    return player_color_from_network_id(player_id);
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

const char *reconcile_mode_name(uint8_t mode) {
    switch (mode) {
    case 0:
        return "OFF";
    case 1:
        return "RECON";
    case 2:
        return "SNAP";
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

glm::vec3 minigame_color(MiniGameType type) {
    switch (type) {
    case MiniGameType::Snake:
        return glm::vec3(0.15f, 0.85f, 0.25f);
    case MiniGameType::Golf:
        return glm::vec3(0.20f, 0.72f, 0.95f);
    case MiniGameType::Tetris:
        return glm::vec3(0.86f, 0.42f, 0.92f);
    case MiniGameType::Racing:
        return glm::vec3(1.0f, 0.55f, 0.20f);
    case MiniGameType::TicTacToe:
        return glm::vec3(0.95f, 0.95f, 0.32f);
    default:
        return glm::vec3(0.8f, 0.8f, 0.8f);
    }
}
}

void Engine::init(void *window_handle, RenderBackendType backend_type, const EngineRuntimeOptions &options) {
    runtime_options = options;

    EnginePhysicsSettings settings{};
    settings.solver_backend = runtime_options.physics_backend;
    physics.init(settings);
    if (settings.solver_backend == PhysicsSolverBackend::AvbdExperimental) {
        if (AvbdSolver *avbd = physics.avbd()) {
            avbd->create_minimal_test_scene();
            spdlog::info(
                "AVBD physics enabled (bodies={}, vertices={})",
                avbd->bodies().size(),
                avbd->vertices().size());
        }
    }
    if (!net_client.init()) {
        spdlog::error("NetClient init failed; multiplayer disabled until restart");
    }
    ui_audio.init();

    build_static_scene();
    local_player = PlayerControllerSystem::spawn_player(collision_world);
    local_player_prev_position = local_player.transform.position;
    vehicle.position = local_player.transform.position + glm::vec3(3.5f, 0.0f, 1.5f);
    vehicle.yaw = 0.3f;
    vehicle.speed = 0.0f;
    vehicle.occupied = false;
    if (k_vehicle_feature_enabled) {
        vehicle.position.y = collision_world.find_spawn_height(
            glm::vec2(vehicle.position.x, vehicle.position.z),
            0.8f,
            1.2f) +
            k_vehicle_wheel_radius;
    }
    vehicle.controller.reset(vehicle.position, vehicle.yaw);
    aircraft.position = local_player.transform.position + glm::vec3(-5.0f, 0.0f, -4.0f);
    aircraft.position.y = collision_world.find_spawn_height(
        glm::vec2(aircraft.position.x, aircraft.position.z), 1.0f, 1.8f) + 1.2f;
    aircraft.yaw = 0.25f;
    aircraft.speed = 0.0f;
    aircraft.occupied = false;
    aircraft.controller.reset(
        aircraft.position,
        glm::vec3(0.0f, aircraft.yaw, 0.0f),
        glm::vec3(0.0f));
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
    if (!net_client.is_initialized()) {
        if (!net_client.init()) {
            spdlog::error("NetClient init failed; cannot connect to {}:{}", host ? host : "(null)", port);
            multiplayer_hint = "Network init failed.";
            return;
        }
    }
    if (!net_client.connect(host, port)) {
        spdlog::error("NetClient connect failed to {}:{}", host ? host : "(null)", port);
        multiplayer_hint = "Connect failed.";
        return;
    }
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
    if (!local_server.init(port, loopback_only)) {
        spdlog::error("Failed to start {} server on {}", loopback_only ? "local-only" : "LAN", port);
        multiplayer_hint = "Failed to start server.";
        return;
    }
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

void Engine::record_prediction_history(uint32_t sim_tick, const InputState &step_input) {
    PredictionHistoryEntry &entry = prediction_history[sim_tick % k_prediction_history_size];
    entry.valid = true;
    entry.tick = sim_tick;
    entry.input = step_input;
    entry.position = local_player.transform.position;
    entry.velocity = local_player.controller.velocity;
}

void Engine::reconcile_local_player_from_snapshot(uint32_t current_sim_tick) {
    if (!has_snapshot || latest_snapshot.player_id == 0 || local_player.network_id == 0) {
        return;
    }
    if (latest_snapshot.player_id != local_player.network_id) {
        return;
    }
    if (vehicle.occupied || aircraft.occupied || runtime_options.noclip) {
        return;
    }

    const uint32_t snapshot_tick = latest_snapshot.tick;
    const uint32_t snapshot_sequence = latest_snapshot.sequence;
    if (snapshot_sequence == 0) {
        return;
    }
    if (snapshot_sequence == last_reconcile_processed_snapshot_sequence) {
        return;
    }
    if (snapshot_tick > current_sim_tick) {
        return;
    }
    if ((current_sim_tick - snapshot_tick) >= k_prediction_history_size) {
        return;
    }

    PredictionHistoryEntry &at_snapshot = prediction_history[snapshot_tick % k_prediction_history_size];
    if (!at_snapshot.valid || at_snapshot.tick != snapshot_tick) {
        return;
    }

    const glm::vec3 authoritative_pos(latest_snapshot.x, latest_snapshot.y, latest_snapshot.z);
    const glm::vec3 authoritative_vel(latest_snapshot.vx, latest_snapshot.vy, latest_snapshot.vz);
    const float pos_error = glm::length(at_snapshot.position - authoritative_pos);
    last_reconcile_pos_error = pos_error;
    last_reconcile_snapshot_tick = snapshot_tick;
    last_reconcile_snapshot_sequence = snapshot_sequence;
    last_reconcile_processed_snapshot_sequence = snapshot_sequence;

    if (reconcile_mode == ReconcileMode::Off) {
        return;
    }

    constexpr float kReconcilePosThreshold = 0.35f;
    if (!std::isfinite(pos_error)) {
        return;
    }
    if (reconcile_mode == ReconcileMode::Threshold && pos_error <= kReconcilePosThreshold) {
        return;
    }

    local_player.transform.position = authoritative_pos;
    local_player.controller.velocity = authoritative_vel;
    local_player.controller.grounded = std::fabs(authoritative_vel.y) < 0.05f;

    reconcile_replay_ticks = 0;
    if (reconcile_mode == ReconcileMode::Snap) {
        reconcile_corrections += 1;
        return;
    }
    for (uint32_t tick = snapshot_tick + 1; tick <= current_sim_tick; ++tick) {
        PredictionHistoryEntry &entry = prediction_history[tick % k_prediction_history_size];
        if (!entry.valid || entry.tick != tick) {
            break;
        }
        (void)PlayerControllerSystem::simulate_fixed(
            local_player,
            entry.input,
            collision_world,
            static_cast<float>(fixed.fixed_dt),
            runtime_options.noclip);
        entry.position = local_player.transform.position;
        entry.velocity = local_player.controller.velocity;
        ++reconcile_replay_ticks;
    }
    reconcile_corrections += 1;
}

void Engine::sync_network_state(uint32_t sim_tick, const InputState &net_input) {
    record_prediction_history(sim_tick, net_input);

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
        reconcile_local_player_from_snapshot(sim_tick);
        has_snapshot = false;
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
        const glm::vec3 sample_velocity(state.vx, state.vy, state.vz);
        const bool should_push_sample =
            render_player.samples.empty() ||
            !(render_player.samples.back().position == target &&
              render_player.samples.back().server_tick == state.tick &&
              render_player.samples.back().velocity == sample_velocity &&
              render_player.samples.back().anim_state == state.anim_state &&
              render_player.samples.back().anim_phase == state.anim_phase &&
              render_player.samples.back().anim_blend == state.anim_blend);
        if (should_push_sample) {
            RemoteRenderPlayer::Sample sample{};
            sample.server_tick = state.tick;
            sample.position = target;
            sample.velocity = sample_velocity;
            sample.anim_state = state.anim_state;
            sample.anim_phase = state.anim_phase;
            sample.anim_blend = state.anim_blend;
            render_player.samples.push_back(sample);
            while (render_player.samples.size() > k_remote_sample_history_max) {
                render_player.samples.pop_front();
            }
        }
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

void Engine::apply_runtime_toggles() {
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
    if (input_state.debug_reconcile_toggle_pressed) {
        const uint8_t next = (static_cast<uint8_t>(reconcile_mode) + 1u) % 3u;
        reconcile_mode = static_cast<ReconcileMode>(next);
        spdlog::info("Reconciliation mode -> {}", reconcile_mode_name(static_cast<uint8_t>(reconcile_mode)));
    }
}

void Engine::process_menu_actions(const InputState &primary_input) {
    GuiMenuActions menu_actions{};
    gui_menu.handle_input(primary_input, runtime_options.devhud, runtime_options.noclip, menu_actions);
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
}

void Engine::update_remote_interpolation(double frame_dt) {
    const float remote_lerp = std::clamp(static_cast<float>(frame_dt) * 12.0f, 0.0f, 1.0f);
    const float remote_rot_lerp = std::clamp(static_cast<float>(frame_dt) * 9.0f, 0.0f, 1.0f);
    uint32_t max_remote_sample_tick = 0;
    bool have_remote_samples = false;
    for (const auto &[player_id, render_player] : remote_render_players) {
        (void)player_id;
        if (!render_player.samples.empty()) {
            max_remote_sample_tick = std::max(max_remote_sample_tick, render_player.samples.back().server_tick);
            have_remote_samples = true;
        }
    }
    if (have_remote_samples) {
        const double desired_tick = static_cast<double>(
            max_remote_sample_tick > k_remote_interp_delay_ticks ? (max_remote_sample_tick - k_remote_interp_delay_ticks) : 0u);
        if (!remote_interp_tick_cursor_initialized) {
            remote_interp_tick_cursor = desired_tick;
            remote_interp_tick_cursor_initialized = true;
        } else {
            remote_interp_tick_cursor += frame_dt / fixed.fixed_dt;
            if (remote_interp_tick_cursor < (desired_tick - 20.0)) {
                remote_interp_tick_cursor = desired_tick;
            }
            remote_interp_tick_cursor = std::min(remote_interp_tick_cursor, desired_tick + 2.0);
        }
    } else {
        remote_interp_tick_cursor_initialized = false;
    }

    for (auto &[player_id, render_player] : remote_render_players) {
        (void)player_id;
        const double target_tick_f = remote_interp_tick_cursor_initialized
            ? remote_interp_tick_cursor
            : static_cast<double>(render_player.samples.empty() ? 0u : render_player.samples.back().server_tick);
        while (render_player.samples.size() >= 3 &&
               static_cast<double>(render_player.samples[1].server_tick) <= target_tick_f) {
            render_player.samples.pop_front();
        }

        glm::vec3 predicted_target = render_player.target_position + render_player.velocity * 0.035f;
        if (!render_player.samples.empty()) {
            if (render_player.samples.size() >= 2) {
                const auto &a = render_player.samples[0];
                const auto &b = render_player.samples[1];
                if (target_tick_f <= static_cast<double>(a.server_tick)) {
                    predicted_target = a.position;
                    render_player.velocity = a.velocity;
                    render_player.anim_state = a.anim_state;
                    render_player.anim_phase = a.anim_phase;
                    render_player.anim_blend = a.anim_blend;
                } else if (target_tick_f <= static_cast<double>(b.server_tick)) {
                    const float dt_ticks = static_cast<float>(std::max<uint32_t>(1u, b.server_tick - a.server_tick));
                    const float t = std::clamp(static_cast<float>(target_tick_f - static_cast<double>(a.server_tick)) / dt_ticks, 0.0f, 1.0f);
                    predicted_target = glm::mix(a.position, b.position, t);
                    render_player.velocity = glm::mix(a.velocity, b.velocity, t);
                    render_player.anim_state = (t < 0.5f) ? a.anim_state : b.anim_state;
                    render_player.anim_phase = glm::mix(a.anim_phase, b.anim_phase, t);
                    render_player.anim_blend = glm::mix(a.anim_blend, b.anim_blend, t);
                } else {
                    const auto &latest = render_player.samples.back();
                    const double ahead_ticks = std::max(0.0, target_tick_f - static_cast<double>(latest.server_tick));
                    const float extrap = std::clamp(static_cast<float>(ahead_ticks * fixed.fixed_dt), 0.0f, 0.10f);
                    predicted_target = latest.position + latest.velocity * extrap;
                    render_player.velocity = latest.velocity;
                    render_player.anim_state = latest.anim_state;
                    render_player.anim_phase = latest.anim_phase;
                    render_player.anim_blend = latest.anim_blend;
                }
            } else {
                const auto &latest = render_player.samples.back();
                predicted_target = latest.position;
                render_player.velocity = latest.velocity;
                render_player.anim_state = latest.anim_state;
                render_player.anim_phase = latest.anim_phase;
                render_player.anim_blend = latest.anim_blend;
            }
            render_player.target_position = predicted_target;
        }

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
}

void Engine::tick(double frame_dt) {
    last_frame_dt = frame_dt;

    apply_runtime_toggles();
    process_menu_actions(input_state);

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

    handle_minigame_interaction(gameplay_input);
    if (!active_minigame.active) {
        handle_vehicle_interaction(gameplay_input);
        handle_aircraft_interaction(gameplay_input);
    }

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
        if (active_minigame.active) {
            update_active_minigame(step_input, static_cast<float>(fixed.fixed_dt));
            last_collision_debug = PlayerCollisionDebug{};
        } else {
            update_vehicle_sim(step_input, static_cast<float>(fixed.fixed_dt));
            update_aircraft_sim(step_input, static_cast<float>(fixed.fixed_dt));
        }
        if (active_minigame.active) {
            last_collision_debug = PlayerCollisionDebug{};
        } else if (vehicle.occupied || aircraft.occupied) {
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

    update_remote_interpolation(frame_dt);

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
    constexpr uint64_t k_world_seed = 0x0DDF00D5EEDull;
    world_chunk.generate_heightmap_terrain_seeded(k_world_seed, 0, 0);
    collision_world = VoxelCollisionWorld(&world_chunk);

    scene = RenderScene{};
    scene.opaque_meshes.push_back(world_chunk.build_sky_placeholder(240.0f));
    scene.opaque_meshes.push_back(world_chunk.build_naive_mesh());
    scene.debug_grid = world_chunk.build_debug_grid(160.0f, 1.0f);

    const glm::vec2 center(
        static_cast<float>(VoxelChunk::CHUNK_X) * 0.5f,
        static_cast<float>(VoxelChunk::CHUNK_Z) * 0.5f);
    vehicle.position = glm::vec3(center.x - 5.0f, 0.0f, center.y - 2.0f);
    vehicle.yaw = 0.0f;
    vehicle.speed = 0.0f;
    vehicle.occupied = false;
    const float ground_y = collision_world.find_spawn_height(
        glm::vec2(vehicle.position.x, vehicle.position.z),
        0.8f,
        1.2f);
    vehicle.position.y = ground_y + k_vehicle_wheel_radius;
    vehicle.controller.reset(vehicle.position, vehicle.yaw);

    aircraft.position = glm::vec3(center.x + 6.0f, 0.0f, center.y - 3.0f);
    aircraft.position.y = collision_world.find_spawn_height(
        glm::vec2(aircraft.position.x, aircraft.position.z), 1.0f, 1.8f) + 1.2f;
    aircraft.yaw = -0.2f;
    aircraft.speed = 0.0f;
    aircraft.occupied = false;
    aircraft.controller.reset(
        aircraft.position,
        glm::vec3(0.0f, aircraft.yaw, 0.0f),
        glm::vec3(0.0f));

    minigame_hotspots.clear();
    const auto spawn_hotspot = [&](MiniGameType type, const glm::vec3 &base) {
        MiniGameHotspot hotspot{};
        hotspot.type = type;
        hotspot.position = base;
        constexpr float k_probe_radius = 0.35f;
        constexpr float k_probe_height = 1.8f;
        hotspot.position.y = collision_world.find_spawn_height(
            glm::vec2(base.x, base.z),
            k_probe_radius,
            k_probe_height) +
            0.05f;
        hotspot.interact_radius = k_minigame_interact_radius;
        minigame_hotspots.push_back(hotspot);
    };
    spawn_hotspot(MiniGameType::Snake, glm::vec3(center.x - 16.0f, 0.0f, center.y + 10.0f));
    spawn_hotspot(MiniGameType::Golf, glm::vec3(center.x + 14.0f, 0.0f, center.y - 12.0f));
    spawn_hotspot(MiniGameType::Tetris, glm::vec3(center.x + 15.0f, 0.0f, center.y + 12.0f));
    spawn_hotspot(MiniGameType::Racing, glm::vec3(center.x - 18.0f, 0.0f, center.y - 8.0f));
    spawn_hotspot(MiniGameType::TicTacToe, glm::vec3(center.x, 0.0f, center.y - 16.0f));
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
    return vehicle.position + rotate_y(glm::vec3(0.0f, k_vehicle_body_height + 0.5f, 0.0f), vehicle.yaw + k_vehicle_visual_yaw_offset);
}

glm::vec3 Engine::aircraft_seat_world_position() const {
    return aircraft.position + rotate_y(glm::vec3(0.0f, 0.8f, 0.0f), aircraft.yaw);
}

void Engine::handle_vehicle_interaction(const InputState &input) {
    if (!k_vehicle_feature_enabled) {
        vehicle.occupied = false;
        return;
    }
    if (!input.interact_pressed || gui_menu.open() || !gameplay_started) {
        return;
    }
    if (aircraft.occupied) {
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

void Engine::handle_aircraft_interaction(const InputState &input) {
    if (!k_vehicle_feature_enabled) {
        aircraft.occupied = false;
        return;
    }
    if (!input.interact_pressed || gui_menu.open() || !gameplay_started) {
        return;
    }
    if (vehicle.occupied) {
        return;
    }

    if (aircraft.occupied) {
        aircraft.occupied = false;
        const glm::vec3 exit_candidate = aircraft.position + rotate_y(glm::vec3(-2.5f, -1.0f, -1.2f), aircraft.yaw);
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

    const float d = glm::length(local_player.transform.position - aircraft.position);
    if (d <= k_aircraft_interact_radius) {
        aircraft.occupied = true;
        local_player.transform.position = aircraft_seat_world_position();
        local_player.controller.velocity = glm::vec3(0.0f);
        local_player.controller.grounded = false;
    }
}

void Engine::handle_minigame_interaction(const InputState &input) {
    nearby_minigame_hotspot = -1;
    minigame_hint.clear();

    if (!gameplay_started || gui_menu.open()) {
        return;
    }

    if (active_minigame.active) {
        if (active_minigame_hotspot >= 0 && active_minigame_hotspot < static_cast<int>(minigame_hotspots.size())) {
            nearby_minigame_hotspot = active_minigame_hotspot;
            const MiniGameHotspot &hotspot = minigame_hotspots[static_cast<size_t>(active_minigame_hotspot)];
            minigame_hint = std::string(minigame_name(hotspot.type)) + " in progress";
            if (active_minigame.completed) {
                minigame_hint += active_minigame.victory ? " [WIN]" : " [DONE]";
                minigame_hint += " - press F or E to exit";
            } else {
                minigame_hint += " - press C/CTRL to exit";
            }
        }

        if ((input.interact_pressed && active_minigame.completed) || input.crouch_held) {
            active_minigame.active = false;
            active_minigame_hotspot = -1;
            local_player.controller.velocity = glm::vec3(0.0f);
            local_player.controller.grounded = true;
            minigame_hint = "Exited minigame.";
        }
        return;
    }

    float best_distance = 1e9f;
    int best_index = -1;
    for (size_t i = 0; i < minigame_hotspots.size(); ++i) {
        const MiniGameHotspot &hotspot = minigame_hotspots[i];
        const glm::vec2 to_hotspot(
            local_player.transform.position.x - hotspot.position.x,
            local_player.transform.position.z - hotspot.position.z);
        const float distance = glm::length(to_hotspot);
        if (distance <= hotspot.interact_radius && distance < best_distance) {
            best_distance = distance;
            best_index = static_cast<int>(i);
        }
    }

    nearby_minigame_hotspot = best_index;
    if (best_index >= 0) {
        const MiniGameHotspot &hotspot = minigame_hotspots[static_cast<size_t>(best_index)];
        minigame_hint = std::string("Press F or E to play ") + minigame_name(hotspot.type);
        if (input.interact_pressed) {
            minigame_begin(active_minigame, hotspot.type, static_cast<uint32_t>(fixed.tick + 17u * static_cast<uint32_t>(best_index + 1)));
            active_minigame_hotspot = best_index;
            vehicle.occupied = false;
            aircraft.occupied = false;
            local_player.controller.velocity = glm::vec3(0.0f);
            local_player.controller.grounded = true;
            minigame_hint = std::string("Started ") + minigame_name(hotspot.type);
        }
    }
}

void Engine::update_active_minigame(const InputState &input, float dt) {
    if (!active_minigame.active || active_minigame_hotspot < 0 || active_minigame_hotspot >= static_cast<int>(minigame_hotspots.size())) {
        return;
    }

    const MiniGameHotspot &hotspot = minigame_hotspots[static_cast<size_t>(active_minigame_hotspot)];
    const float seat_y = collision_world.find_spawn_height(
        glm::vec2(hotspot.position.x, hotspot.position.z),
        local_player.controller.capsuleRadius,
        local_player.controller.capsuleHeight) +
        0.05f;
    local_player.transform.position = glm::vec3(hotspot.position.x, seat_y, hotspot.position.z);
    local_player.controller.velocity = glm::vec3(0.0f);
    local_player.controller.grounded = true;
    minigame_tick(active_minigame, input, dt);
}

void Engine::update_vehicle_sim(const InputState &input, float dt) {
    if (!k_vehicle_feature_enabled) {
        vehicle.occupied = false;
        vehicle.speed = 0.0f;
        return;
    }
    const bool sandbox_drive = runtime_options.vehicle_sandbox && !vehicle.occupied;
    if (!vehicle.occupied && !sandbox_drive) {
        vehicle.speed = 0.0f;
        return;
    }
    if (aircraft.occupied) {
        return;
    }
    VehicleControlInput control{};
    control.throttle = input.move.y;
    control.steer = input.move.x;
    control.brake = input.move.y < -0.05f ? std::min(1.0f, -input.move.y) : 0.0f;
    control.handbrake = input.crouch_held ? 1.0f : 0.0f;
    vehicle.controller.step(control, collision_world, dt);
    vehicle.position = vehicle.controller.state().kinematic.position;
    vehicle.yaw = vehicle.controller.state().kinematic.yaw;
    vehicle.speed = vehicle.controller.state().telemetry.speed_mps;

    if (vehicle.occupied) {
        local_player.transform.position = vehicle_seat_world_position();
        local_player.transform.rotation = glm::angleAxis(vehicle.yaw + k_vehicle_visual_yaw_offset, glm::vec3(0.0f, 1.0f, 0.0f));
        local_player.controller.velocity = glm::vec3(0.0f);
        local_player.controller.grounded = true;
        local_player.anim_state = PlayerAnimState::Idle;
        local_player.anim_blend = 0.0f;
    }
}

void Engine::update_aircraft_sim(const InputState &input, float dt) {
    if (!k_vehicle_feature_enabled) {
        aircraft.occupied = false;
        return;
    }
    const bool sandbox_fly = runtime_options.vehicle_sandbox && !aircraft.occupied;
    if (!aircraft.occupied && !sandbox_fly) {
        return;
    }

    AircraftControlInput control{};
    control.throttle = std::clamp(input.move.y, 0.0f, 1.0f);
    control.yaw = input.move.x;
    control.pitch = (input.jump_held ? 0.45f : 0.0f) + (input.crouch_held ? -0.35f : 0.0f);
    control.roll = -input.move.x * 0.55f;
    aircraft.controller.step(control, collision_world, dt);

    aircraft.position = aircraft.controller.state().kinematic.position;
    aircraft.yaw = aircraft.controller.state().kinematic.euler.y;
    aircraft.speed = aircraft.controller.state().telemetry.speed_mps;
    constexpr float k_bounds_margin = 2.0f;
    aircraft.position.x = std::clamp(aircraft.position.x, k_bounds_margin, static_cast<float>(VoxelChunk::CHUNK_X) - k_bounds_margin);
    aircraft.position.z = std::clamp(aircraft.position.z, k_bounds_margin, static_cast<float>(VoxelChunk::CHUNK_Z) - k_bounds_margin);

    if (aircraft.occupied) {
        local_player.transform.position = aircraft_seat_world_position();
        local_player.transform.rotation = glm::angleAxis(aircraft.yaw, glm::vec3(0.0f, 1.0f, 0.0f));
        local_player.controller.velocity = aircraft.controller.state().kinematic.velocity;
        local_player.controller.grounded = false;
        local_player.anim_state = PlayerAnimState::Idle;
        local_player.anim_blend = 0.0f;
    }
}

void Engine::refresh_overlay_text() {
    scene.debug_screen = RenderMesh{};

    auto append_screen_rect = [&](float x0, float y0, float x1, float y1, const glm::vec3 &color) {
        RenderMesh rect{};
        const uint32_t base = 0;
        rect.vertices.push_back({glm::vec3(x0, y0, 0.0f), color});
        rect.vertices.push_back({glm::vec3(x1, y0, 0.0f), color});
        rect.vertices.push_back({glm::vec3(x1, y1, 0.0f), color});
        rect.vertices.push_back({glm::vec3(x0, y1, 0.0f), color});
        rect.indices.insert(rect.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
        append_mesh(scene.debug_screen, rect);
    };

    const bool menu_is_open = gui_menu.open();
    if (menu_is_open) {
        const GuiMenuView menu_view = gui_menu.build_view(runtime_options.devhud, runtime_options.noclip, multiplayer_hint);

        float y = 0.86f;
        if (!menu_view.title.empty()) {
            append_mesh(scene.debug_screen, build_screen_text_mesh(menu_view.title, -0.90f, y, 0.0082f, glm::vec3(0.96f, 0.98f, 1.0f)));
            y -= 0.11f;
        }

        for (size_t i = 0; i < menu_view.items.size(); ++i) {
            const bool selected = static_cast<int>(i) == menu_view.selected;
            std::string line = selected ? ("> " + menu_view.items[i]) : ("  " + menu_view.items[i]);
            append_mesh(
                scene.debug_screen,
                build_screen_text_mesh(
                    line,
                    -0.88f,
                    y,
                    0.0069f,
                    selected ? glm::vec3(0.96f, 0.98f, 1.0f) : glm::vec3(0.86f, 0.91f, 0.98f)));
            y -= 0.095f;
        }

        if (!menu_view.guide_lines.empty()) {
            y -= 0.02f;
            for (const std::string &line : menu_view.guide_lines) {
                append_mesh(scene.debug_screen, build_screen_text_mesh(line, -0.88f, y, 0.0059f, glm::vec3(0.80f, 0.88f, 0.97f)));
                y -= 0.072f;
            }
        }

        if (!menu_view.status.empty()) {
            append_mesh(
                scene.debug_screen,
                build_screen_text_mesh(
                    "STATUS: " + menu_view.status,
                    -0.90f,
                    -0.80f,
                    0.0056f,
                    glm::vec3(0.88f, 0.93f, 0.99f)));
        }
    } else if (runtime_options.devhud) {
        char text[768]{};
        const float vehicle_distance = k_vehicle_feature_enabled
            ? glm::length(local_player.transform.position - vehicle.position)
            : 0.0f;
        const float aircraft_distance = k_vehicle_feature_enabled
            ? glm::length(local_player.transform.position - aircraft.position)
            : 0.0f;
        const NetDebugStats client_net_stats = net_client.debug_stats();
        const NetDebugStats server_net_stats = local_server_running ? local_server.debug_stats() : NetDebugStats{};
        std::snprintf(
            text,
            sizeof(text),
            "FPS %.1f DT %.3f FIX %.3f\nP %.1f %.1f %.1f V %.1f %.1f %.1f G %d\nPEN %.3f N %.1f %.1f %.1f\nYAW %.1f PIT %.1f LOOK %.1f %.1f\nRMB %d LOCK %d LKEN %d REM %d\nNET C%d LID %u\nNCL tx/rx pps %u/%u Bps %u/%u inv %llu\nNSV on%d tx/rx pps %u/%u Bps %u/%u snap %u pst %u\nREC %s err %.2f tick %u seq %u replay %u corr %llu\nANIM %s BL %.2f PH %.2f\nVEH %s DIST %.1f\nAIR %s SPD %.1f DIST %.1f",
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
            client_net_stats.tx_packets_per_sec,
            client_net_stats.rx_packets_per_sec,
            client_net_stats.tx_bytes_per_sec,
            client_net_stats.rx_bytes_per_sec,
            static_cast<unsigned long long>(client_net_stats.invalid_packets_total),
            local_server_running ? 1 : 0,
            server_net_stats.tx_packets_per_sec,
            server_net_stats.rx_packets_per_sec,
            server_net_stats.tx_bytes_per_sec,
            server_net_stats.rx_bytes_per_sec,
            server_net_stats.snapshots_sent_per_sec,
            server_net_stats.player_state_broadcasts_per_sec,
            reconcile_mode_name(static_cast<uint8_t>(reconcile_mode)),
            last_reconcile_pos_error,
            last_reconcile_snapshot_tick,
            last_reconcile_snapshot_sequence,
            reconcile_replay_ticks,
            static_cast<unsigned long long>(reconcile_corrections),
            anim_state_name(local_player.anim_state),
            local_player.anim_blend,
            local_player.anim_phase,
            k_vehicle_feature_enabled ? (vehicle.occupied ? "ONBOARD" : "ON FOOT") : "DISABLED",
            vehicle_distance,
            k_vehicle_feature_enabled ? (aircraft.occupied ? "ONBOARD" : "ON FOOT") : "DISABLED",
            aircraft.speed,
            aircraft_distance);
        append_screen_rect(-0.98f, 0.98f, 0.10f, 0.08f, glm::vec3(0.05f, 0.07f, 0.10f));
        append_screen_rect(-0.97f, 0.97f, 0.09f, 0.10f, glm::vec3(0.09f, 0.11f, 0.16f));
        append_mesh(scene.debug_screen, build_screen_text_mesh(text, -0.95f, 0.92f, 0.0049f, glm::vec3(0.95f, 0.95f, 0.82f)));
    }

    if (!menu_is_open && (active_minigame.active || nearby_minigame_hotspot >= 0)) {
        std::string panel = active_minigame.active
            ? minigame_status_text(active_minigame)
            : "MINIGAME HOTSPOT";
        if (!minigame_hint.empty()) {
            panel += "\n";
            panel += minigame_hint;
        }
        if (active_minigame.active && !active_minigame.completed) {
            panel += "\nWASD+SPACE+F/E play, C/CTRL exits";
        }

        append_screen_rect(-0.98f, -0.12f, 0.30f, -0.40f, glm::vec3(0.04f, 0.06f, 0.08f));
        append_screen_rect(-0.97f, -0.13f, 0.28f, -0.39f, glm::vec3(0.08f, 0.10f, 0.13f));
        append_mesh(scene.debug_screen, build_screen_text_mesh(panel, -0.95f, -0.16f, 0.0054f, glm::vec3(0.91f, 0.96f, 1.0f)));
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
    render_stats.menu_text.clear();
}

void Engine::rebuild_dynamic_debug_mesh() {
    scene.debug_world = RenderMesh{};
    const bool collision_debug_enabled = runtime_options.debug_collision;
    const bool render_skeleton_only = gui_menu.character() == GuiMenu::Character::Skeleton;
    const SkinnedModel *selected_player_model = nullptr;
    if (gui_menu.character() == GuiMenu::Character::Fox && has_fox_player_model) {
        selected_player_model = &fox_player_model;
    } else if (gui_menu.character() == GuiMenu::Character::Humanoid && has_humanoid_player_model) {
        selected_player_model = &humanoid_player_model;
    }
    const bool render_skinned_avatar = !render_skeleton_only && selected_player_model != nullptr;
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
        const float visual_yaw = vehicle.yaw + k_vehicle_visual_yaw_offset;
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
            p[i] = vehicle.position + rotate_y(local_center + lc[i], visual_yaw);
        }
        append_vehicle_quad(p[0], p[1], p[3], p[2], color);
        append_vehicle_quad(p[4], p[6], p[7], p[5], color);
        append_vehicle_quad(p[0], p[2], p[6], p[4], color);
        append_vehicle_quad(p[1], p[5], p[7], p[3], color);
        append_vehicle_quad(p[2], p[3], p[7], p[6], color);
        append_vehicle_quad(p[0], p[4], p[5], p[1], color);
    };

    if (k_vehicle_feature_enabled && !runtime_options.debug_collision_only) {
        append_vehicle_box(
            glm::vec3(0.0f, k_vehicle_body_height * 0.5f, 0.0f),
            glm::vec3(k_vehicle_body_half_width, k_vehicle_body_height * 0.5f, k_vehicle_body_half_length),
            vehicle.occupied ? glm::vec3(0.15f, 0.78f, 0.35f) : glm::vec3(0.85f, 0.62f, 0.22f));
        append_vehicle_box(
            glm::vec3(0.0f, k_vehicle_body_height + 0.28f, -0.1f),
            glm::vec3(0.58f, 0.28f, 0.68f),
            glm::vec3(0.2f, 0.35f, 0.42f));
        append_vehicle_box(
            glm::vec3(0.0f, 0.58f, k_vehicle_body_half_length - 0.22f),
            glm::vec3(0.55f, 0.12f, 0.14f),
            glm::vec3(0.08f, 0.08f, 0.08f));

        const auto &wheel_setup = vehicle.controller.wheel_setup();
        const float visual_yaw = vehicle.yaw + k_vehicle_visual_yaw_offset;
        for (const GroundVehicleWheel &wheel : wheel_setup) {
            const glm::vec3 wheel_offset(wheel.local_mount.x, 0.0f, wheel.local_mount.z);
            append_mesh(
                scene.debug_world,
                build_debug_sphere_mesh(vehicle.position + rotate_y(wheel_offset, visual_yaw), wheel.radius, glm::vec3(0.12f, 0.12f, 0.12f)));
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

        const glm::vec3 aircraft_color = aircraft.occupied ? glm::vec3(0.12f, 0.84f, 0.95f) : glm::vec3(0.42f, 0.70f, 0.95f);
        auto append_aircraft_box = [&](const glm::vec3 &local_center, const glm::vec3 &half_extent) {
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
                p[i] = aircraft.position + rotate_y(local_center + lc[i], aircraft.yaw);
            }
            append_vehicle_quad(p[0], p[1], p[3], p[2], aircraft_color);
            append_vehicle_quad(p[4], p[6], p[7], p[5], aircraft_color);
            append_vehicle_quad(p[0], p[2], p[6], p[4], aircraft_color);
            append_vehicle_quad(p[1], p[5], p[7], p[3], aircraft_color);
            append_vehicle_quad(p[2], p[3], p[7], p[6], aircraft_color);
            append_vehicle_quad(p[0], p[4], p[5], p[1], aircraft_color);
        };

        append_aircraft_box(
            glm::vec3(0.0f, k_aircraft_body_height, 0.0f),
            glm::vec3(k_aircraft_body_width * 0.5f, k_aircraft_body_height, k_aircraft_body_length * 0.5f));
        append_aircraft_box(
            glm::vec3(0.0f, k_aircraft_body_height + 0.1f, -0.2f),
            glm::vec3(2.1f, 0.08f, 0.36f));
        append_aircraft_box(
            glm::vec3(0.0f, k_aircraft_body_height + 0.5f, -1.0f),
            glm::vec3(0.16f, 0.44f, 0.16f));

        append_mesh(
            scene.debug_world,
            build_debug_line_mesh(
                aircraft.position,
                aircraft.position + rotate_y(glm::vec3(0.0f, 0.0f, 4.0f), aircraft.yaw),
                0.05f,
                glm::vec3(0.2f, 0.9f, 1.0f)));
    }

    if (!runtime_options.debug_collision_only) {
        auto append_minigame_voxel = [&](const glm::vec3 &center, const glm::vec3 &half, const glm::vec3 &color) {
            append_mesh(scene.debug_world, build_debug_aabb_mesh(center - half, center + half, color));
        };

        for (size_t i = 0; i < minigame_hotspots.size(); ++i) {
            const MiniGameHotspot &hotspot = minigame_hotspots[i];
            const glm::vec3 color = minigame_color(hotspot.type);
            const bool selected = static_cast<int>(i) == nearby_minigame_hotspot || static_cast<int>(i) == active_minigame_hotspot;
            append_mesh(
                scene.debug_world,
                build_debug_line_mesh(
                    hotspot.position + glm::vec3(0.0f, 0.2f, 0.0f),
                    hotspot.position + glm::vec3(0.0f, 3.0f, 0.0f),
                    selected ? 0.09f : 0.06f,
                    color));
            append_mesh(
                scene.debug_world,
                build_debug_sphere_mesh(
                    hotspot.position + glm::vec3(0.0f, 3.2f, 0.0f),
                    selected ? 0.28f : 0.2f,
                    color));
            append_mesh(
                scene.debug_world,
                build_debug_sphere_mesh(
                    hotspot.position + glm::vec3(0.0f, 0.15f, 0.0f),
                    selected ? 0.17f : 0.12f,
                    color * glm::vec3(1.1f)));
        }

        if (active_minigame.active &&
            active_minigame_hotspot >= 0 &&
            active_minigame_hotspot < static_cast<int>(minigame_hotspots.size())) {
            const MiniGameHotspot &hotspot = minigame_hotspots[static_cast<size_t>(active_minigame_hotspot)];
            const glm::vec3 base = hotspot.position + glm::vec3(-1.6f, 0.25f, -1.4f);
            const glm::vec3 half(0.08f, 0.08f, 0.08f);

            if (active_minigame.type == MiniGameType::Snake) {
                for (int i = 0; i < active_minigame.snake.length; ++i) {
                    const glm::ivec2 c = active_minigame.snake.body[static_cast<size_t>(i)];
                    append_minigame_voxel(
                        base + glm::vec3(c.x * 0.18f, 0.0f, c.y * 0.18f),
                        half,
                        glm::vec3(0.2f, 0.9f, 0.3f));
                }
                append_minigame_voxel(
                    base + glm::vec3(active_minigame.snake.food.x * 0.18f, 0.0f, active_minigame.snake.food.y * 0.18f),
                    half,
                    glm::vec3(0.95f, 0.25f, 0.2f));
            } else if (active_minigame.type == MiniGameType::TicTacToe) {
                for (int y = 0; y < 3; ++y) {
                    for (int x = 0; x < 3; ++x) {
                        const int idx = y * 3 + x;
                        const uint8_t cell = active_minigame.tictactoe.board[static_cast<size_t>(idx)];
                        const glm::vec3 cpos = base + glm::vec3(x * 0.32f, 0.0f, y * 0.32f);
                        append_minigame_voxel(cpos, glm::vec3(0.11f, 0.03f, 0.11f), glm::vec3(0.18f, 0.22f, 0.26f));
                        if (cell == 1) {
                            append_minigame_voxel(cpos + glm::vec3(0.0f, 0.1f, 0.0f), glm::vec3(0.05f), glm::vec3(0.15f, 0.9f, 0.3f));
                        } else if (cell == 2) {
                            append_minigame_voxel(cpos + glm::vec3(0.0f, 0.1f, 0.0f), glm::vec3(0.05f), glm::vec3(0.9f, 0.2f, 0.2f));
                        }
                    }
                }
            } else if (active_minigame.type == MiniGameType::Golf) {
                append_minigame_voxel(base + glm::vec3(active_minigame.golf.ball.x * 0.2f, 0.0f, active_minigame.golf.ball.y * 0.2f), half, glm::vec3(0.9f));
                append_minigame_voxel(base + glm::vec3(active_minigame.golf.hole.x * 0.2f, 0.0f, active_minigame.golf.hole.y * 0.2f), half, glm::vec3(0.2f, 0.6f, 1.0f));
            } else if (active_minigame.type == MiniGameType::Tetris) {
                for (int y = 0; y < TetrisState::k_board_h; ++y) {
                    for (int x = 0; x < TetrisState::k_board_w; ++x) {
                        const uint8_t filled = active_minigame.tetris.board[static_cast<size_t>(y * TetrisState::k_board_w + x)];
                        if (filled == 0) {
                            continue;
                        }
                        append_minigame_voxel(base + glm::vec3(x * 0.13f, y * 0.02f, 0.0f), glm::vec3(0.05f, 0.01f, 0.05f), glm::vec3(0.78f, 0.42f, 0.92f));
                    }
                }
            } else if (active_minigame.type == MiniGameType::Racing) {
                const float progress = active_minigame.racing.track_progress / 65.0f;
                append_minigame_voxel(base + glm::vec3(progress * 1.8f, 0.0f, 0.0f), glm::vec3(0.07f), glm::vec3(1.0f, 0.55f, 0.2f));
            }
        }
    }

    if (!runtime_options.debug_collision_only) {
        if ((!render_skinned_avatar && !render_skeleton_only) || collision_debug_enabled || runtime_options.devhud) {
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
        if (render_skeleton_only || (render_skinned_avatar && (collision_debug_enabled || runtime_options.devhud))) {
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
            if ((!render_skinned_avatar && !render_skeleton_only) || collision_debug_enabled || runtime_options.devhud) {
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
            if (render_skeleton_only) {
                const SkeletonPose p2_pose = SkeletalAnimator::sample_pose(
                    local_player_secondary.anim_state,
                    local_player_secondary.anim_phase,
                    local_player_secondary.anim_blend);
                SkeletalAnimator::append_debug_skeleton(
                    scene.debug_world,
                    p2_pose,
                    local_player_secondary.transform.position + glm::vec3(0.0f, p2_shape.bob, 0.0f),
                    local_player_secondary.transform.rotation,
                    glm::vec3(0.86f, 0.92f, 1.0f),
                    0.010f);
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
