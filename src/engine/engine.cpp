#include "engine/engine.hpp"

#include "engine_gameplay/player/player_controller.hpp"
#include "engine_net/remote_interp.hpp"
#include "engine_net/net_runtime_shared.hpp"
#include "engine_physics/avbd_solver.hpp"

#include <spdlog/spdlog.h>

#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_set>

namespace {
using PerfClock = std::chrono::steady_clock;

constexpr float k_vehicle_body_half_length = 1.35f;
constexpr float k_vehicle_body_half_width = 0.8f;
constexpr float k_vehicle_body_height = 0.65f;
constexpr float k_vehicle_wheel_radius = 0.32f;
constexpr float k_vehicle_interact_radius = 2.1f;
constexpr bool k_vehicle_feature_enabled = false;
constexpr float k_vehicle_visual_yaw_offset = 0.0f;
constexpr float k_aircraft_interact_radius = 4.2f;
constexpr uint32_t k_remote_interp_delay_ticks = 6;
constexpr size_t k_remote_sample_history_max = 16;
constexpr float k_minigame_interact_radius = 6.5f;
constexpr float k_network_chunk_world_size = 16.0f;

glm::vec3 rotate_y(const glm::vec3 &v, float yaw_radians) {
  const float c = std::cos(yaw_radians);
  const float s = std::sin(yaw_radians);
  return glm::vec3(v.x * c - v.z * s, v.y, v.x * s + v.z * c);
}

struct SurfaceFrame {
  glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
  glm::vec3 east = glm::vec3(1.0f, 0.0f, 0.0f);
  glm::vec3 north = glm::vec3(0.0f, 0.0f, 1.0f);
};

SurfaceFrame make_surface_frame(glm::vec3 up_raw) {
  SurfaceFrame frame{};
  if (glm::length(up_raw) > 0.001f) {
    frame.up = glm::normalize(up_raw);
  }
  glm::vec3 ref_axis(0.0f, 1.0f, 0.0f);
  if (std::fabs(glm::dot(frame.up, ref_axis)) > 0.94f) {
    ref_axis = glm::vec3(1.0f, 0.0f, 0.0f);
  }
  frame.east = glm::normalize(glm::cross(ref_axis, frame.up));
  frame.north = glm::normalize(glm::cross(frame.up, frame.east));
  return frame;
}

double elapsed_ms(const PerfClock::time_point &start,
                  const PerfClock::time_point &end) {
  return std::chrono::duration<double, std::milli>(end - start).count();
}

double smooth_metric(double current, double sample, double alpha = 0.25) {
  if (sample < 0.0) {
    sample = 0.0;
  }
  if (current <= 0.0) {
    return sample;
  }
  return current + (sample - current) * alpha;
}

const char *anim_state_name(PlayerAnimState state) {
  return player_anim_state_name(state);
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

glm::quat facing_from_velocity(glm::vec3 velocity, const glm::quat &fallback) {
  const glm::vec2 flat(velocity.x, velocity.z);
  const float speed = glm::length(flat);
  if (speed < 0.08f) {
    return fallback;
  }
  const float yaw = std::atan2(velocity.x, velocity.z);
  return glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f));
}

bool try_load_character_model(const PlatformServices &platform_services,
                              SkinnedModel &model, const char *label,
                              const char *filename, bool &out_loaded) {
  std::string load_error;
  const std::string relative_path =
      std::string("assets/models/player/") + filename;
  for (const std::string &path :
       platform_services.candidate_asset_paths(relative_path)) {
    if (model.load_from_glb(path, load_error)) {
      out_loaded = true;
      spdlog::info("Loaded {} player model from {}", label, path);
      return true;
    }
  }
  spdlog::warn("{} player model not loaded: {}", label, load_error);
  return false;
}

void sync_local_animation_runtime(PlayerEntity &player,
                                  PlayerAnimationRuntime &runtime, float dt) {
  if (runtime.state() != player.anim_state) {
    runtime.request_state(player.anim_state,
                          player_anim_crossfade_seconds(player.anim_state));
  }
  runtime.advance(dt);
  player.animation.state = runtime.state();
  player.anim_previous_state = runtime.previous_state();
  player.anim_previous_phase = runtime.previous_phase_radians();
  player.anim_phase = runtime.phase_radians();
  player.animation.phase = player.anim_phase;
  player.anim_transition_duration =
      player_anim_crossfade_seconds(player.anim_state);
  player.anim_transition_time =
      runtime.transition_alpha() * player.anim_transition_duration;
  player.animation.blend = player.anim_blend;
  if (!runtime.events().empty()) {
    player.last_anim_event = runtime.events().back().type;
  }
  runtime.clear_events();
}

template <typename RemoteRenderPlayerT>
void sync_remote_animation_runtime(RemoteRenderPlayerT &player, float dt) {
  const PlayerAnimState state = static_cast<PlayerAnimState>(player.anim_state);
  player.animation_runtime.sync_external_state(
      state, player.anim_phase, player_anim_crossfade_seconds(state));
  player.animation_runtime.advance_transition(dt);
}

void disable_gameplay_actions(InputState &input) {
  input.move = glm::vec2(0.0f);
  input.jump_pressed = false;
  input.jump_held = false;
  input.interact_pressed = false;
  input.sprint_held = false;
  input.crouch_held = false;
}

} // namespace

void Engine::init(void *window_handle, const EngineRuntimeOptions &options) {
  runtime_options = options;
  platform_services = PlatformServices::desktop_default();
  game_session.reset();
  session_controller.set_devhud_enabled(runtime_options.devhud);
  session_controller.set_noclip_enabled(runtime_options.noclip);
  session_controller.set_gameplay_started(false);
  spdlog::info("Engine init: backend=OpenGL save_path={}",
               platform_services.session_state_path().generic_string());
  gui_menu.set_character(GuiMenu::Character::Capsule);

  EnginePhysicsSettings settings{};
  settings.solver_backend = runtime_options.physics_backend;
  physics.init(settings);
  if (settings.solver_backend == PhysicsSolverBackend::AvbdExperimental) {
    if (AvbdSolver *avbd = physics.avbd()) {
      avbd->create_minimal_test_scene();
      spdlog::info("AVBD physics enabled (bodies={}, vertices={})",
                   avbd->bodies().size(), avbd->vertices().size());
    }
  }
  if (!net_client.init()) {
    spdlog::error("NetClient init failed; multiplayer disabled until restart");
  }
  ui_audio.init();

  build_static_scene();
  world_state.load_persistent_state(platform_services);
  local_player = PlayerControllerSystem::spawn_player(collision_world);
  if (runtime_options.spherical_planet &&
      world_state.spherical_planet_radius > 0.0f) {
    const float spawn_radius = world_state.spherical_planet_radius +
                               local_player.controller.capsuleHeight * 0.52f;
    local_player.transform.position =
        world_state.spherical_planet_center +
        glm::vec3(0.0f, spawn_radius, 0.0f);
    local_player.transform.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    local_player.controller.velocity = glm::vec3(0.0f);
    local_player.controller.grounded = true;
    local_player.camera_rig.pitch = -8.0f;
    local_player.camera_rig.distance = 4.2f;
  }
  local_player_prev_position = local_player.transform.position;
  local_player_animation.reset(local_player.anim_state);
  vehicle.position =
      local_player.transform.position + glm::vec3(3.5f, 0.0f, 1.5f);
  vehicle.yaw = 0.3f;
  vehicle.speed = 0.0f;
  vehicle.occupied = false;
  if (runtime_options.spherical_planet &&
      world_state.spherical_planet_radius > 0.0f) {
    const glm::vec3 spawn_up =
        glm::normalize(local_player.transform.position -
                       world_state.spherical_planet_center);
    const SurfaceFrame spawn_frame = make_surface_frame(spawn_up);
    const glm::vec3 vehicle_seed = local_player.transform.position +
                                   spawn_frame.east * 3.5f +
                                   spawn_frame.north * 1.5f;
    const glm::vec3 vehicle_dir =
        glm::normalize(vehicle_seed - world_state.spherical_planet_center);
    vehicle.position = world_state.spherical_planet_center +
                       vehicle_dir * (world_state.spherical_planet_radius +
                                      k_vehicle_wheel_radius + 0.06f);
    vehicle.yaw = std::atan2(spawn_frame.north.x, spawn_frame.north.z);
  } else if (k_vehicle_feature_enabled) {
    vehicle.position.y =
        collision_world.find_spawn_height(
            glm::vec2(vehicle.position.x, vehicle.position.z), 0.8f, 1.2f) +
        k_vehicle_wheel_radius;
  }
  vehicle.controller.reset(vehicle.position, vehicle.yaw);
  aircraft.position =
      local_player.transform.position + glm::vec3(-5.0f, 0.0f, -4.0f);
  aircraft.yaw = 0.25f;
  aircraft.speed = 0.0f;
  aircraft.throttle_cmd = 0.0f;
  aircraft.occupied = false;
  if (runtime_options.spherical_planet &&
      world_state.spherical_planet_radius > 0.0f) {
    const glm::vec3 spawn_up =
        glm::normalize(local_player.transform.position -
                       world_state.spherical_planet_center);
    const SurfaceFrame spawn_frame = make_surface_frame(spawn_up);
    const glm::vec3 aircraft_seed = local_player.transform.position -
                                    spawn_frame.east * 5.0f -
                                    spawn_frame.north * 4.0f;
    const glm::vec3 aircraft_dir =
        glm::normalize(aircraft_seed - world_state.spherical_planet_center);
    aircraft.position = world_state.spherical_planet_center +
                        aircraft_dir * (world_state.spherical_planet_radius +
                                        1.8f);
    aircraft.yaw = std::atan2(-spawn_frame.north.x, -spawn_frame.north.z);
  } else {
    aircraft.position.y =
        collision_world.find_spawn_height(
            glm::vec2(aircraft.position.x, aircraft.position.z), 1.0f, 1.8f) +
        1.2f;
  }
  aircraft.controller.reset(
      aircraft.position, glm::vec3(0.0f, aircraft.yaw, 0.0f), glm::vec3(0.0f));
  if (runtime_options.splitscreen) {
    local_player_secondary =
        PlayerControllerSystem::spawn_player(collision_world);
    local_player_secondary.network_id = 2;
    local_player_secondary.transform.position.x += 2.5f;
    local_player_secondary.camera_rig.yaw = 180.0f;
    local_player_secondary_prev_position =
        local_player_secondary.transform.position;
    local_player_secondary_animation.reset(local_player_secondary.anim_state);
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
               local_player.network_id, local_player.transform.position.x,
               local_player.transform.position.y,
               local_player.transform.position.z);

  try_load_character_model(platform_services, humanoid_player_model, "humanoid",
                           "CesiumMan.glb", has_humanoid_player_model);
  gui_menu.set_character(has_humanoid_player_model
                             ? GuiMenu::Character::Humanoid
                             : GuiMenu::Character::Capsule);

  try {
    renderer.init(window_handle);
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
      spdlog::error("NetClient init failed; cannot connect to {}:{}",
                    host ? host : "(null)", port);
      multiplayer_hint = "Network init failed.";
      return;
    }
  }
  if (!net_client.connect(host, port)) {
    spdlog::error("NetClient connect failed to {}:{}", host ? host : "(null)",
                  port);
    multiplayer_hint = "Connect failed.";
    return;
  }
  NetChunkInterest interest{};
  interest.center_x = 0;
  interest.center_z = 0;
  interest.radius = 2;
  net_client.set_chunk_interest(interest);
  last_chunk_interest = interest;
  has_last_chunk_interest = true;
  net_connect_elapsed = 0.0;
  multiplayer_hint = std::string("Connecting to ") + (host ? host : "server") +
                     ":" + std::to_string(port) + "...";
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
    spdlog::error("Failed to start {} server on {}",
                  loopback_only ? "local-only" : "LAN", port);
    multiplayer_hint = "Failed to start server.";
    return;
  }
  local_server_loopback = loopback_only;
  local_server_running = true;
  spdlog::info("Started {} server on {}", loopback_only ? "local-only" : "LAN",
               port);
}

void Engine::stop_client_session() {
  net_client.disconnect();
  searching_nearby = false;
  net_connect_elapsed = 0.0;
  has_last_chunk_interest = false;
  has_snapshot = false;
  latest_snapshot = NetSnapshot{};
  remote_players.clear();
  remote_render_players.clear();
  local_player.network_id = 1;
  local_replication.network_id = 1;
  world_state.reset_streamed_chunks(runtime_options.spherical_planet);
  world_state.rebuild_streamed_chunk_scene(world_chunk, scene,
                                           runtime_options.spherical_planet);
  renderer.upload_scene(scene);
}

void Engine::leave_session() {
  stop_client_session();
  lan_discovery.stop();
  if (local_server_running) {
    local_server.shutdown();
    local_server_running = false;
  }
  local_server_loopback = true;
  multiplayer_hint = "Left session.";
}

void Engine::set_input(const InputState &input_primary,
                       const InputState &input_secondary, bool touch_mode) {
  input_state = input_primary;
  input_state_secondary = input_secondary;
  touch_input_mode = touch_mode;
}

void Engine::shutdown() {
  world_state.save_persistent_state(platform_services);
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

void Engine::record_prediction_history(uint32_t sim_tick,
                                       const InputState &step_input) {
  PredictionHistoryEntry &entry =
      prediction_history[sim_tick % k_prediction_history_size];
  entry.valid = true;
  entry.tick = sim_tick;
  entry.input = step_input;
  entry.position = local_player.transform.position;
  entry.velocity = local_player.controller.velocity;
}

void Engine::reconcile_local_player_from_snapshot(uint32_t current_sim_tick) {
  if (!has_snapshot || latest_snapshot.player_id == 0 ||
      local_player.network_id == 0) {
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

  PredictionHistoryEntry &at_snapshot =
      prediction_history[snapshot_tick % k_prediction_history_size];
  if (!at_snapshot.valid || at_snapshot.tick != snapshot_tick) {
    return;
  }

  const glm::vec3 authoritative_pos(latest_snapshot.x, latest_snapshot.y,
                                    latest_snapshot.z);
  const glm::vec3 authoritative_vel(latest_snapshot.vx, latest_snapshot.vy,
                                    latest_snapshot.vz);
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
  if (reconcile_mode == ReconcileMode::Threshold &&
      pos_error <= kReconcilePosThreshold) {
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
    PredictionHistoryEntry &entry =
        prediction_history[tick % k_prediction_history_size];
    if (!entry.valid || entry.tick != tick) {
      break;
    }
    const FixedStep &fixed = game_session.fixed_step();
    (void)PlayerControllerSystem::simulate_fixed(
        local_player, entry.input, collision_world,
        static_cast<float>(fixed.fixed_dt), runtime_options.noclip);
    entry.position = local_player.transform.position;
    entry.velocity = local_player.controller.velocity;
    ++reconcile_replay_ticks;
  }
  reconcile_corrections += 1;
}

void Engine::sync_network_state(uint32_t sim_tick,
                                const InputState &net_input) {
  record_prediction_history(sim_tick, net_input);

  NetTickInput input{};
  input.tick = sim_tick;
  input.move_x = net_input.move.x;
  input.move_y = net_input.move.y;
  input.camera_yaw_deg = local_player.camera_rig.yaw;
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
    if (!std::isfinite(target.x) || !std::isfinite(target.y) ||
        !std::isfinite(target.z)) {
      target = glm::vec3(state.x, local_player.transform.position.y, state.z);
    }
    if (!render_player.initialized) {
      render_player.position = target;
      render_player.orientation = local_player.transform.rotation;
      render_player.target_orientation = render_player.orientation;
      render_player.animation_runtime.reset(
          static_cast<PlayerAnimState>(state.anim_state));
      render_player.initialized = true;
    }
    render_player.target_position = target;
    render_player.velocity = glm::vec3(state.vx, state.vy, state.vz);
    render_player.target_orientation = facing_from_velocity(
        render_player.velocity, render_player.target_orientation);
    render_player.anim_state = state.anim_state;
    render_player.anim_phase = state.anim_phase;
    render_player.anim_blend = state.anim_blend;
    const glm::vec3 sample_velocity(state.vx, state.vy, state.vz);
    net_remote_push_sample(render_player, state.tick, target, sample_velocity,
                           state.anim_state, state.anim_phase, state.anim_blend,
                           k_remote_sample_history_max);
    seen_remote_ids.insert(player_id);
  }

  for (auto it = remote_render_players.begin();
       it != remote_render_players.end();) {
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
    runtime_options.debug_collision_only =
        !runtime_options.debug_collision_only;
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
    spdlog::info("Reconciliation mode -> {}",
                 reconcile_mode_name(static_cast<uint8_t>(reconcile_mode)));
  }
}

void Engine::process_menu_actions(const InputState &primary_input) {
  const RuntimeSessionMenuCallbacks callbacks{
      .leave_session = [this]() { leave_session(); },
      .host_local = [this]() {
        leave_session();
        start_local_server(7777, true);
        connect("127.0.0.1", 7777);
        lan_discovery.stop();
        searching_nearby = false;
        multiplayer_hint = "Hosting this device only.";
      },
      .host_lan = [this]() {
        leave_session();
        start_local_server(7777, false);
        connect("127.0.0.1", 7777);
        lan_discovery.start_host(7777, "VOXOV Host");
        searching_nearby = false;
        multiplayer_hint =
            "Hosting Wi-Fi game. Tell friends: Multiplayer > Join Nearby.";
      },
      .join_nearby = [this]() {
        stop_client_session();
        if (local_server_running) {
          local_server.shutdown();
          local_server_running = false;
          local_server_loopback = true;
        }
        lan_discovery.stop();
        lan_discovery.start_client();
        searching_nearby = true;
        net_connect_elapsed = 0.0;
        multiplayer_hint = "Searching nearby Wi-Fi hosts...";
      }};
  const RuntimeMenuResult menu_result =
      session_controller.handle_menu_input(primary_input, gui_menu, callbacks);
  if (menu_result.ui_move_sfx) {
    ui_audio.play_move();
  }
  if (menu_result.ui_select_sfx) {
    ui_audio.play_click();
  }
  runtime_options.devhud = session_controller.devhud_enabled();
  runtime_options.noclip = session_controller.noclip_enabled();
  if (local_server_running && !local_server_loopback) {
    lan_discovery.pump();
  }
  const NetClientConnectionState connection_state =
      net_client.connection_state();
  if (connection_state == NetClientConnectionState::Connecting) {
    net_connect_elapsed += last_frame_dt;
    if (net_connect_elapsed >= 5.0) {
      stop_client_session();
      multiplayer_hint = "Connection timed out.";
    }
  } else {
    net_connect_elapsed = 0.0;
  }
  if (connection_state != last_net_connection_state) {
    if (connection_state == NetClientConnectionState::Connected) {
      multiplayer_hint.clear();
    } else if (last_net_connection_state ==
                   NetClientConnectionState::Connected &&
               multiplayer_hint != "Left session.") {
      multiplayer_hint = "Disconnected from server.";
    }
    last_net_connection_state = connection_state;
  }
  if (searching_nearby &&
      connection_state == NetClientConnectionState::Disconnected) {
    lan_discovery.pump();
    LanHostEntry host{};
    if (lan_discovery.pop_host(host)) {
      if (net_client.connect(host.ip.c_str(), host.port)) {
        NetChunkInterest interest{};
        interest.center_x = 0;
        interest.center_z = 0;
        interest.radius = 2;
        net_client.set_chunk_interest(interest);
        last_chunk_interest = interest;
        has_last_chunk_interest = true;
        searching_nearby = false;
        net_connect_elapsed = 0.0;
        multiplayer_hint = "Joining " + host.name + " (" + host.ip + ")";
      } else {
        multiplayer_hint = "Join failed. Retrying discovery...";
      }
    }
  } else if (searching_nearby &&
             connection_state == NetClientConnectionState::Connected) {
    searching_nearby = false;
  }
  if (menu_result.reset_camera_requested) {
    local_player.camera_rig.yaw = 180.0f;
    local_player.camera_rig.pitch = -12.0f;
    local_player.camera_rig.distance = 5.0f;
  }
}

RuntimeSessionSnapshot Engine::session_snapshot() const {
  RuntimeSessionSnapshot snapshot{};
  snapshot.connection_state = net_client.connection_state();
  snapshot.searching_nearby = searching_nearby;
  snapshot.hosting_local = local_server_running && local_server_loopback;
  snapshot.hosting_lan = local_server_running && !local_server_loopback;
  snapshot.has_session_info = net_client.has_session_info();
  if (snapshot.has_session_info) {
    snapshot.session_info = net_client.session_info();
  }
  snapshot.connect_target_host = net_client.connect_target_host();
  snapshot.connect_target_port = net_client.connect_target_port();
  snapshot.status_hint = multiplayer_hint;
  return snapshot;
}

void Engine::update_remote_interpolation(double frame_dt) {
  const FixedStep &fixed = game_session.fixed_step();
  net_remote_interpolate(remote_render_players, remote_interp_tick_cursor,
                         remote_interp_tick_cursor_initialized, frame_dt,
                         fixed.fixed_dt, k_remote_interp_delay_ticks);

  const float remote_rot_lerp =
      std::clamp(static_cast<float>(frame_dt) * 9.0f, 0.0f, 1.0f);
  for (auto &[player_id, render_player] : remote_render_players) {
    (void)player_id;
    render_player.orientation = glm::normalize(
        glm::slerp(render_player.orientation, render_player.target_orientation,
                   remote_rot_lerp));
    sync_remote_animation_runtime(render_player, static_cast<float>(frame_dt));
  }
}

void Engine::tick(double frame_dt) {
  const PerfClock::time_point frame_cpu_start = PerfClock::now();
  const FixedStep &fixed = game_session.fixed_step();
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
  if (!session_controller.gameplay_started()) {
    disable_gameplay_actions(gameplay_input);
    gameplay_input_secondary = gameplay_input;
  }

  handle_objective_interaction(gameplay_input);
  handle_minigame_interaction(gameplay_input);
  if (!active_minigame.active) {
    handle_vehicle_interaction(gameplay_input);
    handle_aircraft_interaction(gameplay_input);
  }

  PlayerControllerSystem::update_camera_rig(local_player, gameplay_input,
                                            touch_input_mode,
                                            static_cast<float>(frame_dt));
  if (runtime_options.splitscreen) {
    PlayerControllerSystem::update_camera_rig(local_player_secondary,
                                              gameplay_input_secondary, false,
                                              static_cast<float>(frame_dt));
  }

  bool jump_consumed = false;
  const RuntimeGameSessionCallbacks callbacks{
      .pump_server = [this]() {
        if (local_server_running) {
          local_server.pump();
        }
      },
      .simulate_step =
          [this, &gameplay_input, &gameplay_input_secondary,
           &jump_consumed](const RuntimeGameSessionStepContext &step) {
            local_player_prev_position = local_player.transform.position;
            InputState step_input = gameplay_input;
            if (jump_consumed) {
              step_input.jump_pressed = false;
            }
            if (active_minigame.active) {
              update_active_minigame(step_input, step.dt);
              last_collision_debug = PlayerCollisionDebug{};
            } else {
              update_vehicle_sim(step_input, step.dt);
              update_aircraft_sim(step_input, step.dt);
            }
            if (active_minigame.active) {
              last_collision_debug = PlayerCollisionDebug{};
            } else if (vehicle.occupied || aircraft.occupied) {
              last_collision_debug = PlayerCollisionDebug{};
            } else if (runtime_options.spherical_planet &&
                       world_state.spherical_planet_radius > 0.0f) {
              update_spherical_player_sim(step_input, step.dt);
              last_collision_debug = PlayerCollisionDebug{};
            } else {
              last_collision_debug = PlayerControllerSystem::simulate_fixed(
                  local_player, step_input, collision_world, step.dt,
                  runtime_options.noclip);
            }

            if (runtime_options.splitscreen) {
              local_player_secondary_prev_position =
                  local_player_secondary.transform.position;
              InputState step_input_secondary = gameplay_input_secondary;
              last_collision_debug_secondary =
                  PlayerControllerSystem::simulate_fixed(
                      local_player_secondary, step_input_secondary,
                      collision_world, step.dt, runtime_options.noclip);
            }
            sync_local_animation_runtime(local_player, local_player_animation,
                                         step.dt);
            if (runtime_options.splitscreen) {
              sync_local_animation_runtime(local_player_secondary,
                                           local_player_secondary_animation,
                                           step.dt);
            }
            jump_consumed = jump_consumed || input_state.jump_pressed;

            sync_network_state(static_cast<uint32_t>(step.tick), step_input);

            local_replication.position = local_player.transform.position;
            local_replication.velocity = local_player.controller.velocity;

            physics.step(step.dt);
          }};
  game_session.advance(frame_dt, callbacks);
  const double fixed_cpu_ms = game_session.fixed_cpu_ms();
  const uint32_t fixed_steps_this_frame = game_session.fixed_steps_last_frame();

  input_state.jump_pressed = false;
  input_state.interact_pressed = false;

  const float alpha = static_cast<float>(
      std::clamp(fixed.accumulator / fixed.fixed_dt, 0.0, 1.0));
  const glm::vec3 local_player_render_position = glm::mix(
      local_player_prev_position, local_player.transform.position, alpha);
  update_third_person_camera(local_player, local_player_render_position,
                             camera);
  if (runtime_options.splitscreen) {
    const glm::vec3 secondary_render_position =
        glm::mix(local_player_secondary_prev_position,
                 local_player_secondary.transform.position, alpha);
    update_third_person_camera(local_player_secondary,
                               secondary_render_position, secondary_camera);
  }

  fps_accumulator += frame_dt;
  fps_frames++;
  if (fps_accumulator >= 0.3) {
    render_stats.fps = static_cast<double>(fps_frames) / fps_accumulator;
    fps_accumulator = 0.0;
    fps_frames = 0;
  }

  if (runtime_options.devhud) {
    const MovementDebug movement_debug =
        PlayerControllerSystem::compute_movement_vectors(
            local_player.camera_rig.yaw, input_state.move);

    const bool any_non_zero = std::fabs(input_state.look_delta.x) > 0.0001f ||
                              std::fabs(input_state.look_delta.y) > 0.0001f ||
                              std::fabs(input_state.move.x) > 0.0001f ||
                              std::fabs(input_state.move.y) > 0.0001f ||
                              input_state.key_w || input_state.key_a ||
                              input_state.key_s || input_state.key_d;

    log_accumulator += frame_dt;
    if (any_non_zero && log_accumulator >= 0.2) {
      log_accumulator = 0.0;
      spdlog::info(
          "devhud dt={:.4f} fixed_dt={:.4f} mouse_dx={:.2f} mouse_dy={:.2f} "
          "keys[W{} A{} S{} D{}] axes[MoveX={:.2f} MoveY={:.2f} LookX={:.2f} "
          "LookY={:.2f}] cam[yaw={:.2f} pitch={:.2f} f=({:.2f},{:.2f},{:.2f}) "
          "r=({:.2f},{:.2f},{:.2f})] move[desired=({:.2f},{:.2f},{:.2f}) "
          "strafeRight=({:.2f},{:.2f},{:.2f})] pos=({:.2f},{:.2f},{:.2f}) "
          "vel=({:.2f},{:.2f},{:.2f}) grounded={} pen={:.3f} "
          "n=({:.2f},{:.2f},{:.2f}) mode[rmb={} lock={} look_en={}] remotes={} "
          "noclip={}",
          frame_dt, fixed.fixed_dt, input_state.look_delta.x,
          input_state.look_delta.y, input_state.key_w ? 1 : 0,
          input_state.key_a ? 1 : 0, input_state.key_s ? 1 : 0,
          input_state.key_d ? 1 : 0, input_state.move.x, input_state.move.y,
          input_state.look_delta.x, input_state.look_delta.y,
          local_player.camera_rig.yaw, local_player.camera_rig.pitch,
          movement_debug.forward.x, movement_debug.forward.y,
          movement_debug.forward.z, movement_debug.right.x,
          movement_debug.right.y, movement_debug.right.z,
          movement_debug.desired.x, movement_debug.desired.y,
          movement_debug.desired.z, movement_debug.right.x,
          movement_debug.right.y, movement_debug.right.z,
          local_player.transform.position.x, local_player.transform.position.y,
          local_player.transform.position.z, local_player.controller.velocity.x,
          local_player.controller.velocity.y,
          local_player.controller.velocity.z,
          local_player.controller.grounded ? 1 : 0,
          last_collision_debug.penetration_correction,
          last_collision_debug.contact_normal.x,
          last_collision_debug.contact_normal.y,
          last_collision_debug.contact_normal.z, input_state.rmb_down ? 1 : 0,
          input_state.pointer_locked ? 1 : 0, input_state.look_enabled ? 1 : 0,
          static_cast<int>(remote_players.size()),
          runtime_options.noclip ? 1 : 0);
    }
  }

  update_remote_interpolation(frame_dt);

  render_stats.net_connected = net_client.is_connected();
  render_stats.net_local_player_id = local_player.network_id;
  render_stats.net_remote_count =
      static_cast<uint32_t>(remote_render_players.size());
  render_stats.frame_ms =
      smooth_metric(render_stats.frame_ms, last_frame_dt * 1000.0, 0.20);
  render_stats.fixed_cpu_ms =
      smooth_metric(render_stats.fixed_cpu_ms, fixed_cpu_ms, 0.25);
  render_stats.fixed_steps = fixed_steps_this_frame;

  uint32_t chunk_packets_this_frame = 0;
  uint32_t chunk_changes_this_frame = 0;
  const bool scene_changed = world_state.consume_chunk_stream_updates(
      net_client, last_chunk_interest, has_last_chunk_interest,
      runtime_options.spherical_planet, chunk_packets_this_frame,
      chunk_changes_this_frame);
  render_stats.chunk_packets = chunk_packets_this_frame;
  render_stats.chunk_changes = chunk_changes_this_frame;
  render_stats.streamed_chunk_count =
      static_cast<uint32_t>(world_state.streamed_chunks.size());

  if (net_client.is_connected()) {
    NetChunkInterest interest{};
    interest.center_x = static_cast<int16_t>(std::floor(
        local_player.transform.position.x / k_network_chunk_world_size));
    interest.center_z = static_cast<int16_t>(std::floor(
        local_player.transform.position.z / k_network_chunk_world_size));
    interest.radius = 2;
    if (!has_last_chunk_interest ||
        interest.center_x != last_chunk_interest.center_x ||
        interest.center_z != last_chunk_interest.center_z ||
        interest.radius != last_chunk_interest.radius) {
      net_client.set_chunk_interest(interest);
      last_chunk_interest = interest;
      has_last_chunk_interest = true;
    }
  }

  refresh_overlay_text();
  if (!runtime_options.debug_freeze || frozen_debug_world.vertices.empty()) {
    rebuild_dynamic_debug_mesh();
    if (runtime_options.debug_freeze) {
      frozen_debug_world = scene.debug_world;
    }
  } else {
    scene.debug_world = frozen_debug_world;
  }
  if (scene_changed) {
    world_state.rebuild_streamed_chunk_scene(world_chunk, scene,
                                             runtime_options.spherical_planet);
    renderer.upload_scene(scene);
  } else {
    renderer.update_dynamic_meshes(scene.debug_world, scene.debug_screen);
  }

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
  ctx.debug_xray =
      runtime_options.debug_collision && runtime_options.debug_xray;
  const PerfClock::time_point render_cpu_start = PerfClock::now();
  renderer.begin_frame(ctx, render_stats);
  renderer.end_frame();

  const double render_cpu_ms = elapsed_ms(render_cpu_start, PerfClock::now());
  const double frame_cpu_ms = elapsed_ms(frame_cpu_start, PerfClock::now());
  render_stats.render_cpu_ms =
      smooth_metric(render_stats.render_cpu_ms, render_cpu_ms, 0.25);
  render_stats.cpu_ms = smooth_metric(render_stats.cpu_ms, frame_cpu_ms, 0.25);
}

const RenderStats &Engine::stats() const { return render_stats; }

void Engine::build_static_scene() {
  world_state.initialize(runtime_options.spherical_planet, world_chunk,
                         collision_world, scene);

  const glm::vec2 center(static_cast<float>(VoxelChunk::CHUNK_X) * 0.5f,
                         static_cast<float>(VoxelChunk::CHUNK_Z) * 0.5f);
  vehicle.position = glm::vec3(center.x - 5.0f, 0.0f, center.y - 2.0f);
  vehicle.yaw = 0.0f;
  vehicle.speed = 0.0f;
  vehicle.occupied = false;
  const float ground_y = collision_world.find_spawn_height(
      glm::vec2(vehicle.position.x, vehicle.position.z), 0.8f, 1.2f);
  vehicle.position.y = ground_y + k_vehicle_wheel_radius;
  vehicle.controller.reset(vehicle.position, vehicle.yaw);

  aircraft.position = glm::vec3(center.x + 6.0f, 0.0f, center.y - 3.0f);
  aircraft.position.y =
      collision_world.find_spawn_height(
          glm::vec2(aircraft.position.x, aircraft.position.z), 1.0f, 1.8f) +
      1.2f;
  aircraft.yaw = -0.2f;
  aircraft.speed = 0.0f;
  aircraft.throttle_cmd = 0.0f;
  aircraft.occupied = false;
  aircraft.controller.reset(
      aircraft.position, glm::vec3(0.0f, aircraft.yaw, 0.0f), glm::vec3(0.0f));

  minigame_hotspots.clear();
  const auto spawn_hotspot = [&](MiniGameType type, const glm::vec3 &base) {
    MiniGameHotspot hotspot{};
    hotspot.type = type;
    hotspot.position = base;
    constexpr float k_probe_radius = 0.35f;
    constexpr float k_probe_height = 1.8f;
    hotspot.position.y =
        collision_world.find_spawn_height(glm::vec2(base.x, base.z),
                                          k_probe_radius, k_probe_height) +
        0.05f;
    hotspot.interact_radius = k_minigame_interact_radius;
    minigame_hotspots.push_back(hotspot);
  };
  if (runtime_options.spherical_planet) {
    const auto spawn_spherical_hotspot = [&](MiniGameType type,
                                             const glm::vec3 &dir_raw) {
      const glm::vec3 dir = glm::normalize(dir_raw);
      const glm::vec3 ray_start =
          world_state.spherical_planet_center +
          dir * (world_state.spherical_planet_radius * 2.6f);
      float hit_dist = 0.0f;
      glm::vec3 position =
          world_state.spherical_planet_center +
          dir * (world_state.spherical_planet_radius + 0.12f);
      if (collision_world.raycast(
              ray_start, -dir, world_state.spherical_planet_radius * 3.2f,
              hit_dist)) {
        position = ray_start - dir * hit_dist + dir * 0.14f;
      }

      MiniGameHotspot hotspot{};
      hotspot.type = type;
      hotspot.position = position;
      hotspot.interact_radius = std::max(k_minigame_interact_radius, 7.0f);
      minigame_hotspots.push_back(hotspot);
    };

    spawn_spherical_hotspot(MiniGameType::Snake,
                            glm::vec3(-0.55f, 0.82f, 0.20f));
    spawn_spherical_hotspot(MiniGameType::Golf,
                            glm::vec3(0.58f, 0.77f, -0.18f));
    spawn_spherical_hotspot(MiniGameType::Tetris,
                            glm::vec3(0.15f, 0.90f, 0.42f));
    spawn_spherical_hotspot(MiniGameType::Racing,
                            glm::vec3(-0.18f, 0.72f, -0.62f));
    spawn_spherical_hotspot(MiniGameType::TicTacToe,
                            glm::vec3(0.52f, 0.80f, 0.38f));
  } else {
    spawn_hotspot(MiniGameType::Snake,
                  glm::vec3(center.x - 16.0f, 0.0f, center.y + 10.0f));
    spawn_hotspot(MiniGameType::Golf,
                  glm::vec3(center.x + 14.0f, 0.0f, center.y - 12.0f));
    spawn_hotspot(MiniGameType::Tetris,
                  glm::vec3(center.x + 15.0f, 0.0f, center.y + 12.0f));
    spawn_hotspot(MiniGameType::Racing,
                  glm::vec3(center.x - 18.0f, 0.0f, center.y - 8.0f));
    spawn_hotspot(MiniGameType::TicTacToe,
                  glm::vec3(center.x, 0.0f, center.y - 16.0f));
  }
}

void Engine::update_third_person_camera(PlayerEntity &player,
                                        Camera &out_camera) {
  update_third_person_camera(player, player.transform.position, out_camera);
}

void Engine::update_third_person_camera(PlayerEntity &player,
                                        const glm::vec3 &render_position,
                                        Camera &out_camera) {
  if (runtime_options.spherical_planet &&
      world_state.spherical_planet_radius > 0.0f) {
    const glm::vec3 to_player =
        render_position - world_state.spherical_planet_center;
    const glm::vec3 local_up = (glm::length(to_player) > 0.001f)
                                   ? glm::normalize(to_player)
                                   : glm::vec3(0.0f, 1.0f, 0.0f);

    glm::vec3 ref_axis(0.0f, 1.0f, 0.0f);
    if (std::fabs(glm::dot(local_up, ref_axis)) > 0.94f) {
      ref_axis = glm::vec3(1.0f, 0.0f, 0.0f);
    }
    const glm::vec3 east = glm::normalize(glm::cross(ref_axis, local_up));
    const glm::vec3 north = glm::normalize(glm::cross(local_up, east));

    const float yaw = player.camera_rig.yaw * 0.01745329251994329577f;
    const float pitch = player.camera_rig.pitch * 0.01745329251994329577f;
    const glm::vec3 tangent_forward =
        glm::normalize(north * std::cos(yaw) + east * std::sin(yaw));
    const glm::vec3 orbit_forward = glm::normalize(
        tangent_forward * std::cos(pitch) + local_up * std::sin(pitch));
    const glm::vec3 pivot =
        render_position + local_up * player.camera_rig.pivotHeight;

    float camera_distance = player.camera_rig.distance;
    float hit_distance = 0.0f;
    if (collision_world.raycast(pivot, -orbit_forward,
                                player.camera_rig.distance, hit_distance)) {
      camera_distance =
          std::max(player.camera_rig.minDistance, hit_distance - 0.15f);
    }

    const glm::vec3 camera_pos = pivot - orbit_forward * camera_distance;
    out_camera.transform.position = camera_pos;
    out_camera.transform.euler_radians = glm::vec3(0.0f);
    out_camera.set_view_override(glm::lookAt(camera_pos, pivot, local_up));
    return;
  }

  out_camera.clear_view_override();
  const glm::vec3 pivot =
      render_position + glm::vec3(0.0f, player.camera_rig.pivotHeight, 0.0f);
  const glm::vec3 orbit_forward =
      PlayerControllerSystem::orbit_forward_from_angles(
          player.camera_rig.yaw, player.camera_rig.pitch);
  float camera_distance = player.camera_rig.distance;
  float hit_distance = 0.0f;
  if (collision_world.raycast(pivot, -orbit_forward, player.camera_rig.distance,
                              hit_distance)) {
    camera_distance =
        std::max(player.camera_rig.minDistance, hit_distance - 0.15f);
  }
  const glm::vec3 camera_pos = pivot - orbit_forward * camera_distance;
  const glm::vec3 view_dir = glm::normalize(pivot - camera_pos);
  out_camera.transform.position = camera_pos;
  out_camera.transform.euler_radians.y = std::atan2(-view_dir.x, -view_dir.z);
  out_camera.transform.euler_radians.x =
      std::asin(std::clamp(view_dir.y, -1.0f, 1.0f));
  out_camera.transform.euler_radians.z = 0.0f;
}

glm::vec3 Engine::vehicle_seat_world_position() const {
  if (runtime_options.spherical_planet &&
      world_state.spherical_planet_radius > 0.0f) {
    const glm::vec3 up =
        glm::normalize(vehicle.position - world_state.spherical_planet_center);
    return vehicle.position + up * (k_vehicle_body_height + 0.5f);
  }
  return vehicle.position +
         rotate_y(glm::vec3(0.0f, k_vehicle_body_height + 0.5f, 0.0f),
                  vehicle.yaw + k_vehicle_visual_yaw_offset);
}

glm::vec3 Engine::aircraft_seat_world_position() const {
  if (runtime_options.spherical_planet &&
      world_state.spherical_planet_radius > 0.0f) {
    const glm::vec3 up =
        glm::normalize(aircraft.position - world_state.spherical_planet_center);
    return aircraft.position + up * 0.8f;
  }
  return aircraft.position +
         rotate_y(glm::vec3(0.0f, 0.8f, 0.0f), aircraft.yaw);
}

void Engine::handle_vehicle_interaction(const InputState &input) {
  if (!k_vehicle_feature_enabled) {
    vehicle.occupied = false;
    return;
  }
  if (!input.interact_pressed || gui_menu.open() ||
      !session_controller.gameplay_started()) {
    return;
  }
  if (aircraft.occupied) {
    return;
  }

  if (vehicle.occupied) {
    vehicle.occupied = false;
    if (runtime_options.spherical_planet &&
        world_state.spherical_planet_radius > 0.0f) {
      const glm::vec3 up =
          glm::normalize(vehicle.position - world_state.spherical_planet_center);
      const SurfaceFrame frame = make_surface_frame(up);
      glm::vec3 exit_candidate = vehicle.position - frame.east * 1.8f;
      glm::vec3 exit_dir =
          glm::normalize(exit_candidate - world_state.spherical_planet_center);
      const float shell_radius = world_state.spherical_planet_radius +
                                 local_player.controller.capsuleHeight * 0.52f;
      local_player.transform.position =
          world_state.spherical_planet_center + exit_dir * shell_radius;
    } else {
      const glm::vec3 exit_candidate =
          vehicle.position +
          rotate_y(glm::vec3(-1.8f, 0.0f, 0.0f), vehicle.yaw);
      local_player.transform.position = exit_candidate;
      local_player.transform.position.y =
          collision_world.find_spawn_height(
              glm::vec2(local_player.transform.position.x,
                        local_player.transform.position.z),
              local_player.controller.capsuleRadius,
              local_player.controller.capsuleHeight) +
          0.05f;
    }
    local_player.controller.velocity = glm::vec3(0.0f);
    local_player.controller.grounded = true;
    return;
  }

  const float d =
      glm::length(local_player.transform.position - vehicle.position);
  if (d <= k_vehicle_interact_radius) {
    vehicle.occupied = true;
    local_player.transform.position = vehicle_seat_world_position();
    local_player.camera_rig.yaw =
        glm::degrees(vehicle.yaw + k_vehicle_visual_yaw_offset);
    local_player.controller.velocity = glm::vec3(0.0f);
    local_player.controller.grounded = true;
  }
}

void Engine::handle_aircraft_interaction(const InputState &input) {
  if (!k_vehicle_feature_enabled) {
    aircraft.occupied = false;
    return;
  }
  if (!input.interact_pressed || gui_menu.open() ||
      !session_controller.gameplay_started()) {
    return;
  }
  if (vehicle.occupied) {
    return;
  }

  if (aircraft.occupied) {
    aircraft.occupied = false;
    aircraft.throttle_cmd = 0.0f;
    if (runtime_options.spherical_planet &&
        world_state.spherical_planet_radius > 0.0f) {
      const glm::vec3 up =
          glm::normalize(aircraft.position -
                         world_state.spherical_planet_center);
      const SurfaceFrame frame = make_surface_frame(up);
      glm::vec3 exit_candidate =
          aircraft.position - frame.east * 2.4f - frame.north * 0.9f;
      glm::vec3 exit_dir =
          glm::normalize(exit_candidate - world_state.spherical_planet_center);
      const float shell_radius = world_state.spherical_planet_radius +
                                 local_player.controller.capsuleHeight * 0.52f;
      local_player.transform.position =
          world_state.spherical_planet_center + exit_dir * shell_radius;
    } else {
      const glm::vec3 exit_candidate =
          aircraft.position +
          rotate_y(glm::vec3(-2.5f, -1.0f, -1.2f), aircraft.yaw);
      local_player.transform.position = exit_candidate;
      local_player.transform.position.y =
          collision_world.find_spawn_height(
              glm::vec2(local_player.transform.position.x,
                        local_player.transform.position.z),
              local_player.controller.capsuleRadius,
              local_player.controller.capsuleHeight) +
          0.05f;
    }
    local_player.controller.velocity = glm::vec3(0.0f);
    local_player.controller.grounded = true;
    return;
  }

  const float d =
      glm::length(local_player.transform.position - aircraft.position);
  if (d <= k_aircraft_interact_radius) {
    aircraft.occupied = true;
    aircraft.throttle_cmd = 0.0f;
    local_player.transform.position = aircraft_seat_world_position();
    local_player.camera_rig.yaw = glm::degrees(aircraft.yaw);
    local_player.controller.velocity = glm::vec3(0.0f);
    local_player.controller.grounded = false;
  }
}

void Engine::handle_objective_interaction(const InputState &input) {
  world_state.nearby_objective_node = -1;
  world_state.objective_hint.clear();

  if (!session_controller.gameplay_started() || gui_menu.open()) {
    return;
  }
  if (active_minigame.active) {
    return;
  }

  if (world_state.objective_round_complete) {
    world_state.objective_hint =
        "Round complete. Re-open from the menu to run again.";
    return;
  }

  if (world_state.extraction_unlocked) {
    const float extraction_distance = glm::length(glm::vec2(
        local_player.transform.position.x - world_state.extraction_zone_position.x,
        local_player.transform.position.z -
            world_state.extraction_zone_position.z));
    world_state.objective_hint = "All nodes active. Reach extraction.";
    if (extraction_distance <= world_state.extraction_zone_radius) {
      world_state.objective_round_complete = true;
      world_state.objective_hint =
          "Extraction complete. Objective loop cleared.";
      world_state.save_persistent_state(platform_services);
    }
    return;
  }

  float best_distance = 1e9f;
  int best_index = -1;
  for (size_t i = 0; i < world_state.objective_nodes.size(); ++i) {
    const auto &node = world_state.objective_nodes[i];
    if (node.activated) {
      continue;
    }
    const float distance = glm::length(
        glm::vec2(local_player.transform.position.x - node.position.x,
                  local_player.transform.position.z - node.position.z));
    if (distance <= node.interact_radius && distance < best_distance) {
      best_distance = distance;
      best_index = static_cast<int>(i);
    }
  }

  world_state.nearby_objective_node = best_index;
  if (best_index >= 0) {
    world_state.objective_hint = "Press F or E to activate objective node";
    if (input.interact_pressed) {
      auto &node = world_state.objective_nodes[static_cast<size_t>(best_index)];
      node.activated = true;
      world_state.activated_objective_count += 1;
      world_state.objective_hint = "Objective node activated.";
      if (world_state.activated_objective_count >=
          static_cast<int>(world_state.objective_nodes.size())) {
        world_state.extraction_unlocked = true;
        world_state.objective_hint = "All nodes active. Return to extraction.";
      }
      world_state.save_persistent_state(platform_services);
    }
  } else {
    world_state.objective_hint =
        "Activate " +
        std::to_string(
            std::max(0, static_cast<int>(world_state.objective_nodes.size()) -
                            world_state.activated_objective_count)) +
        " remaining node(s).";
  }
}

void Engine::handle_minigame_interaction(const InputState &input) {
  nearby_minigame_hotspot = -1;
  minigame_hint.clear();

  if (!session_controller.gameplay_started() || gui_menu.open()) {
    return;
  }

  if (active_minigame.active) {
    if (active_minigame_hotspot >= 0 &&
        active_minigame_hotspot < static_cast<int>(minigame_hotspots.size())) {
      nearby_minigame_hotspot = active_minigame_hotspot;
      const MiniGameHotspot &hotspot =
          minigame_hotspots[static_cast<size_t>(active_minigame_hotspot)];
      minigame_hint = std::string(minigame_name(hotspot.type)) + " in progress";
      if (active_minigame.completed) {
        minigame_hint += active_minigame.victory ? " [WIN]" : " [DONE]";
        minigame_hint += " - press F or E to exit";
      } else {
        minigame_hint += " - press C/CTRL to exit";
      }
    }

    if ((input.interact_pressed && active_minigame.completed) ||
        input.crouch_held) {
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
    const MiniGameHotspot &hotspot =
        minigame_hotspots[static_cast<size_t>(best_index)];
    minigame_hint =
        std::string("Press F or E to play ") + minigame_name(hotspot.type);
    if (input.interact_pressed) {
      const FixedStep &fixed = game_session.fixed_step();
      minigame_begin(
          active_minigame, hotspot.type,
          static_cast<uint32_t>(fixed.tick +
                                17u * static_cast<uint32_t>(best_index + 1)));
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
  if (!active_minigame.active || active_minigame_hotspot < 0 ||
      active_minigame_hotspot >= static_cast<int>(minigame_hotspots.size())) {
    return;
  }

  const MiniGameHotspot &hotspot =
      minigame_hotspots[static_cast<size_t>(active_minigame_hotspot)];
  if (runtime_options.spherical_planet &&
      world_state.spherical_planet_radius > 0.0f) {
    const glm::vec3 up =
        glm::normalize(hotspot.position - world_state.spherical_planet_center);
    const float shell_radius = world_state.spherical_planet_radius +
                               local_player.controller.capsuleHeight * 0.52f;
    local_player.transform.position =
        world_state.spherical_planet_center + up * shell_radius;
    const float yaw = local_player.camera_rig.yaw * 0.01745329251994329577f;
    const SurfaceFrame frame = make_surface_frame(up);
    glm::vec3 forward = glm::normalize(frame.north * std::cos(yaw) +
                                       frame.east * std::sin(yaw));
    glm::vec3 right = glm::cross(forward, up);
    if (glm::length(right) > 0.001f) {
      right = glm::normalize(right);
      forward = glm::normalize(glm::cross(up, right));
      local_player.transform.rotation =
          glm::quat_cast(glm::mat3(right, up, forward));
    }
  } else {
    const float seat_y = collision_world.find_spawn_height(
                             glm::vec2(hotspot.position.x, hotspot.position.z),
                             local_player.controller.capsuleRadius,
                             local_player.controller.capsuleHeight) +
                         0.05f;
    local_player.transform.position =
        glm::vec3(hotspot.position.x, seat_y, hotspot.position.z);
    local_player.transform.rotation =
        glm::angleAxis(local_player.camera_rig.yaw * 0.01745329251994329577f,
                       glm::vec3(0.0f, 1.0f, 0.0f));
  }
  local_player.camera_rig.distance =
      std::clamp(local_player.camera_rig.distance, 3.8f, 4.8f);
  local_player.camera_rig.pitch =
      std::clamp(local_player.camera_rig.pitch, -15.0f, 10.0f);
  local_player.controller.velocity = glm::vec3(0.0f);
  local_player.controller.grounded = true;

  const bool has_minigame_movement =
      std::fabs(input.move.x) > 0.1f || std::fabs(input.move.y) > 0.1f;
  local_player.locomotion.state = has_minigame_movement
                                      ? PlayerLocomotionState::Walk
                                      : PlayerLocomotionState::Idle;
  local_player.animation.state = has_minigame_movement
                                     ? PlayerAnimState::LocomotionWalk
                                     : PlayerAnimState::Idle;
  local_player.anim_state = has_minigame_movement
                                ? PlayerAnimState::LocomotionWalk
                                : PlayerAnimState::Idle;
  local_player.animation.blend = has_minigame_movement ? 0.35f : 0.0f;
  local_player.anim_blend = has_minigame_movement ? 0.35f : 0.0f;

  InputState minigame_input = input;
  // Allow SPACE as an action key inside minigames in addition to interact.
  minigame_input.interact_pressed =
      minigame_input.interact_pressed || input.jump_pressed;
  minigame_tick(active_minigame, minigame_input, dt);
}

void Engine::update_vehicle_sim(const InputState &input, float dt) {
  if (!k_vehicle_feature_enabled) {
    vehicle.occupied = false;
    vehicle.speed = 0.0f;
    last_vehicle_control = VehicleControlInput{};
    return;
  }
  const bool sandbox_drive =
      runtime_options.vehicle_sandbox && !vehicle.occupied;
  if (!vehicle.occupied && !sandbox_drive) {
    vehicle.speed = 0.0f;
    last_vehicle_control = VehicleControlInput{};
    return;
  }
  if (aircraft.occupied) {
    last_vehicle_control = VehicleControlInput{};
    return;
  }
  VehicleControlInput control{};
  const float steer_axis =
      (std::fabs(input.move.x) > 0.05f) ? input.move.x : 0.0f;
  const float desired_throttle = (std::fabs(input.move.y) > 0.05f)
                                     ? std::clamp(-input.move.y, -1.0f, 1.0f)
                                     : 0.0f;
  const glm::vec3 forward(std::sin(vehicle.yaw), 0.0f, std::cos(vehicle.yaw));
  const float signed_speed =
      glm::dot(vehicle.controller.state().kinematic.velocity, forward);
  const bool oppose_motion =
      desired_throttle * signed_speed < -0.6f && std::fabs(signed_speed) > 1.0f;

  control.throttle = oppose_motion ? 0.0f : desired_throttle;
  control.steer = steer_axis;
  control.brake =
      oppose_motion ? std::min(1.0f, std::fabs(desired_throttle)) : 0.0f;
  control.handbrake = input.crouch_held ? 1.0f : 0.0f;
  last_vehicle_control = control;
  vehicle.controller.step(control, collision_world, dt);
  vehicle.position = vehicle.controller.state().kinematic.position;
  vehicle.yaw = vehicle.controller.state().kinematic.yaw;
  vehicle.speed = vehicle.controller.state().telemetry.speed_mps;

  if (vehicle.occupied) {
    local_player.transform.position = vehicle_seat_world_position();
    local_player.transform.rotation = glm::angleAxis(
        vehicle.yaw + k_vehicle_visual_yaw_offset, glm::vec3(0.0f, 1.0f, 0.0f));
    local_player.controller.velocity = glm::vec3(0.0f);
    local_player.controller.grounded = true;
    local_player.locomotion.state = PlayerLocomotionState::Idle;
    local_player.animation.state = PlayerAnimState::Idle;
    local_player.anim_state = PlayerAnimState::Idle;
    local_player.animation.blend = 0.0f;
    local_player.anim_blend = 0.0f;
  }
}

void Engine::update_aircraft_sim(const InputState &input, float dt) {
  if (!k_vehicle_feature_enabled) {
    aircraft.occupied = false;
    aircraft.throttle_cmd = 0.0f;
    last_aircraft_control = AircraftControlInput{};
    return;
  }
  if (!aircraft.occupied) {
    aircraft.speed = 0.0f;
    aircraft.throttle_cmd = 0.0f;
    last_aircraft_control = AircraftControlInput{};
    return;
  }

  AircraftControlInput control{};
  const float key_throttle_delta =
      ((input.key_w ? 1.0f : 0.0f) - (input.key_s ? 1.0f : 0.0f)) * dt * 0.9f;
  aircraft.throttle_cmd =
      std::clamp(aircraft.throttle_cmd + key_throttle_delta, 0.0f, 1.0f);
  if (!input.key_w && !input.key_s && std::fabs(input.move.y) > 0.2f) {
    aircraft.throttle_cmd =
        std::clamp((input.move.y + 1.0f) * 0.5f, 0.0f, 1.0f);
  }
  control.throttle = aircraft.throttle_cmd;
  control.yaw = input.move.x;
  control.pitch =
      (input.jump_held ? 0.45f : 0.0f) + (input.crouch_held ? -0.35f : 0.0f);
  control.roll = -input.move.x * 0.55f;
  last_aircraft_control = control;
  aircraft.controller.step(control, collision_world, dt);

  aircraft.position = aircraft.controller.state().kinematic.position;
  aircraft.yaw = aircraft.controller.state().kinematic.euler.y;
  aircraft.speed = aircraft.controller.state().telemetry.speed_mps;
  if (!runtime_options.spherical_planet) {
    constexpr float k_bounds_margin = 2.0f;
    aircraft.position.x =
        std::clamp(aircraft.position.x, k_bounds_margin,
                   static_cast<float>(VoxelChunk::CHUNK_X) - k_bounds_margin);
    aircraft.position.z =
        std::clamp(aircraft.position.z, k_bounds_margin,
                   static_cast<float>(VoxelChunk::CHUNK_Z) - k_bounds_margin);
  }

  if (aircraft.occupied) {
    local_player.transform.position = aircraft_seat_world_position();
    local_player.transform.rotation =
        glm::angleAxis(aircraft.yaw, glm::vec3(0.0f, 1.0f, 0.0f));
    local_player.controller.velocity =
        aircraft.controller.state().kinematic.velocity;
    local_player.controller.grounded = false;
    local_player.locomotion.state = PlayerLocomotionState::AirborneFall;
    local_player.animation.state = PlayerAnimState::Idle;
    local_player.anim_state = PlayerAnimState::Idle;
    local_player.animation.blend = 0.0f;
    local_player.anim_blend = 0.0f;
  }
}

void Engine::update_spherical_player_sim(const InputState &input, float dt) {
  if (dt <= 0.0f || world_state.spherical_planet_radius <= 0.0f) {
    return;
  }
  const bool was_grounded = local_player.controller.grounded;

  const glm::vec3 to_player =
      local_player.transform.position - world_state.spherical_planet_center;
  const float dist = std::max(glm::length(to_player), 0.001f);
  const glm::vec3 up = to_player / dist;

  glm::vec3 ref_axis(0.0f, 1.0f, 0.0f);
  if (std::fabs(glm::dot(up, ref_axis)) > 0.94f) {
    ref_axis = glm::vec3(1.0f, 0.0f, 0.0f);
  }
  const glm::vec3 east = glm::normalize(glm::cross(ref_axis, up));
  const glm::vec3 north = glm::normalize(glm::cross(up, east));

  glm::vec3 desired = north * input.move.y + east * input.move.x;
  if (glm::length(desired) > 0.001f) {
    desired = glm::normalize(desired);
  }

  float move_speed = local_player.controller.walkSpeed;
  if (input.crouch_held) {
    move_speed = local_player.controller.crawlSpeed;
  } else if (input.sprint_held) {
    move_speed = local_player.controller.sprintSpeed;
  }

  float radial_velocity = glm::dot(local_player.controller.velocity, up);
  const float gravity_mag = std::fabs(local_player.controller.gravity) * 0.9f;
  if (local_player.controller.grounded && input.jump_pressed) {
    radial_velocity = local_player.controller.jumpVelocity;
    local_player.controller.grounded = false;
  }
  radial_velocity -= gravity_mag * dt;

  local_player.controller.velocity =
      desired * move_speed + up * radial_velocity;
  if (glm::length(desired) < 0.001f) {
    glm::vec3 tangential = local_player.controller.velocity -
                           up * glm::dot(local_player.controller.velocity, up);
    tangential *= std::clamp(1.0f - 6.0f * dt, 0.0f, 1.0f);
    local_player.controller.velocity = tangential + up * radial_velocity;
  }
  local_player.transform.position += local_player.controller.velocity * dt;

  const glm::vec3 to_updated =
      local_player.transform.position - world_state.spherical_planet_center;
  const float updated_dist = std::max(glm::length(to_updated), 0.001f);
  const glm::vec3 updated_up = to_updated / updated_dist;
  const float shell_radius = world_state.spherical_planet_radius +
                             local_player.controller.capsuleHeight * 0.52f;
  if (updated_dist < shell_radius + 0.08f) {
    local_player.transform.position =
        world_state.spherical_planet_center + updated_up * shell_radius;
    const float inward_speed =
        glm::dot(local_player.controller.velocity, updated_up);
    if (inward_speed < 0.0f) {
      local_player.controller.velocity -= updated_up * inward_speed;
    }
    local_player.controller.grounded = true;
  } else {
    local_player.controller.grounded = false;
  }

  glm::vec3 forward = desired;
  if (glm::length(forward) < 0.001f) {
    forward = local_player.transform.rotation * glm::vec3(0.0f, 0.0f, 1.0f);
    forward -= updated_up * glm::dot(forward, updated_up);
    if (glm::length(forward) > 0.001f) {
      forward = glm::normalize(forward);
    } else {
      forward = north;
    }
  }
  glm::vec3 right = glm::normalize(glm::cross(forward, updated_up));
  forward = glm::normalize(glm::cross(updated_up, right));
  local_player.transform.rotation =
      glm::quat_cast(glm::mat3(right, updated_up, forward));

  PlayerControllerSystem::update_animation_state(local_player, input, dt, false,
                                                 was_grounded);
}

RuntimeHudSnapshot Engine::build_hud_snapshot() const {
  const FixedStep &fixed = game_session.fixed_step();

  RuntimeHudSnapshot snapshot{};
  snapshot.devhud_enabled = runtime_options.devhud;
  snapshot.menu_view = gui_menu.build_view(
      runtime_options.devhud, runtime_options.noclip,
      session_controller.build_session_context(session_snapshot()));
  snapshot.show_objective_panel = !snapshot.menu_view.open;

  snapshot.objective_status =
      "OBJECTIVES " + std::to_string(world_state.activated_objective_count) +
      "/" + std::to_string(world_state.objective_nodes.size());
  if (world_state.objective_round_complete) {
    snapshot.objective_status += " [COMPLETE]";
  } else if (world_state.extraction_unlocked) {
    snapshot.objective_status += " [EXTRACT]";
  }
  snapshot.objective_hint = world_state.objective_hint;

  if (!snapshot.menu_view.open &&
      (active_minigame.active || nearby_minigame_hotspot >= 0)) {
    if (active_minigame.active) {
      snapshot.show_minigame_panel = true;
      snapshot.minigame_title = minigame_name(active_minigame.type);
      snapshot.minigame_status = minigame_status_text(active_minigame);
      snapshot.minigame_objective = "OBJECTIVE";
      snapshot.minigame_controls = "WASD move  SPACE/F/E action  C/CTRL exit";
      snapshot.minigame_hint = minigame_hint;
      snapshot.minigame_progress = 0.0f;

      switch (active_minigame.type) {
      case MiniGameType::Snake:
        snapshot.minigame_objective =
            "OBJECTIVE: Reach length 24 without colliding";
        snapshot.minigame_controls = "WASD steer snake  C/CTRL exit";
        snapshot.minigame_progress = std::clamp(
            static_cast<float>(active_minigame.snake.length) / 24.0f, 0.0f,
            1.0f);
        break;
      case MiniGameType::Golf: {
        snapshot.minigame_objective =
            "OBJECTIVE: Sink the ball in fewer strokes";
        snapshot.minigame_controls =
            "A/D aim  W/S power  SPACE/F/E swing  C/CTRL exit";
        const float dist =
            glm::length(active_minigame.golf.hole - active_minigame.golf.ball);
        snapshot.minigame_progress =
            std::clamp(1.0f - (dist / 9.5f), 0.0f, 1.0f);
        break;
      }
      case MiniGameType::Tetris:
        snapshot.minigame_objective =
            "OBJECTIVE: Reach score 1200 before topping out";
        snapshot.minigame_controls =
            "A/D move  SPACE rotate  S soft drop  C/CTRL exit";
        snapshot.minigame_progress = std::clamp(
            static_cast<float>(active_minigame.score) / 1200.0f, 0.0f, 1.0f);
        break;
      case MiniGameType::Racing:
        snapshot.minigame_objective = "OBJECTIVE: Complete 3 laps";
        snapshot.minigame_controls = "W/S throttle  A/D steer  C/CTRL exit";
        snapshot.minigame_progress =
            std::clamp((static_cast<float>(active_minigame.racing.lap) +
                        active_minigame.racing.track_progress / 65.0f) /
                           3.0f,
                       0.0f, 1.0f);
        break;
      case MiniGameType::TicTacToe:
        snapshot.minigame_objective =
            "OBJECTIVE: Align 3 marks before the AI";
        snapshot.minigame_controls =
            "WASD move cursor  SPACE/F/E place  C/CTRL exit";
        snapshot.minigame_progress = std::clamp(
            static_cast<float>(active_minigame.tictactoe.turns) / 9.0f, 0.0f,
            1.0f);
        break;
      default:
        break;
      }

      if (active_minigame.completed) {
        snapshot.minigame_controls = "Press F/E to leave";
        snapshot.minigame_progress = 1.0f;
      }
    } else {
      snapshot.show_hotspot_panel = true;
      snapshot.hotspot_text = "MINIGAME HOTSPOT";
      if (!minigame_hint.empty()) {
        snapshot.hotspot_text += "\n";
        snapshot.hotspot_text += minigame_hint;
      }
      snapshot.hotspot_text += "\nPress F or E to start";
    }
  }

  if (runtime_options.devhud) {
    char text[1280]{};
    const float vehicle_distance =
        k_vehicle_feature_enabled
            ? glm::length(local_player.transform.position - vehicle.position)
            : 0.0f;
    const float aircraft_distance =
        k_vehicle_feature_enabled
            ? glm::length(local_player.transform.position - aircraft.position)
            : 0.0f;
    const NetDebugStats client_net_stats = net_client.debug_stats();
    const NetDebugStats server_net_stats =
        local_server_running ? local_server.debug_stats() : NetDebugStats{};
    std::snprintf(
        text, sizeof(text),
        "FPS %.1f FT %.2f CPU %.2f RND %.2f\nFIX dt %.3f CPU %.2f x%u STR in "
        "%u chg %u live %u\nP %.1f %.1f %.1f V %.1f %.1f %.1f G %d\nPEN %.3f "
        "N %.1f %.1f %.1f\nYAW %.1f PIT %.1f LOOK %.1f %.1f\nRMB %d LOCK %d "
        "LKEN %d REM %d\nNET C%d LID %u\nNCL tx/rx pps %u/%u Bps "
        "%u/%u inv %llu\nNSV on%d tx/rx pps %u/%u Bps %u/%u snap %u pst "
        "%u\nREC %s err %.2f tick %u seq %u replay %u corr %llu\nLOCO %s SPD "
        "%.2f GND %d SLP %.1f CYO %.2f BUF %.2f\nANIM %s BL %.2f PH %.2f X "
        "%.2f EVT %s\nPROC lean %.2f bank %.2f land %.2f jump %.2f\nVEH %s "
        "DIST %.1f C[th %.2f br %.2f st %.2f hb %.2f]\nAIR %s SPD %.1f DIST "
        "%.1f C[th %.2f y %.2f p %.2f r %.2f]",
        render_stats.fps, render_stats.frame_ms, render_stats.cpu_ms,
        render_stats.render_cpu_ms, fixed.fixed_dt, render_stats.fixed_cpu_ms,
        render_stats.fixed_steps, render_stats.chunk_packets,
        render_stats.chunk_changes, render_stats.streamed_chunk_count,
        local_player.transform.position.x, local_player.transform.position.y,
        local_player.transform.position.z, local_player.controller.velocity.x,
        local_player.controller.velocity.y, local_player.controller.velocity.z,
        local_player.controller.grounded ? 1 : 0,
        last_collision_debug.penetration_correction,
        last_collision_debug.contact_normal.x,
        last_collision_debug.contact_normal.y,
        last_collision_debug.contact_normal.z, local_player.camera_rig.yaw,
        local_player.camera_rig.pitch, input_state.look_delta.x,
        input_state.look_delta.y, input_state.rmb_down ? 1 : 0,
        input_state.pointer_locked ? 1 : 0, input_state.look_enabled ? 1 : 0,
        static_cast<int>(remote_render_players.size()),
        render_stats.net_connected ? 1 : 0, render_stats.net_local_player_id,
        client_net_stats.tx_packets_per_sec,
        client_net_stats.rx_packets_per_sec, client_net_stats.tx_bytes_per_sec,
        client_net_stats.rx_bytes_per_sec,
        static_cast<unsigned long long>(client_net_stats.invalid_packets_total),
        local_server_running ? 1 : 0, server_net_stats.tx_packets_per_sec,
        server_net_stats.rx_packets_per_sec, server_net_stats.tx_bytes_per_sec,
        server_net_stats.rx_bytes_per_sec,
        server_net_stats.snapshots_sent_per_sec,
        server_net_stats.player_state_broadcasts_per_sec,
        reconcile_mode_name(static_cast<uint8_t>(reconcile_mode)),
        last_reconcile_pos_error, last_reconcile_snapshot_tick,
        last_reconcile_snapshot_sequence, reconcile_replay_ticks,
        static_cast<unsigned long long>(reconcile_corrections),
        player_locomotion_state_name(local_player.locomotion.state),
        local_player.locomotion.move_speed,
        local_player.locomotion.stable_grounded ? 1 : 0,
        local_player.locomotion.slope_angle_deg,
        local_player.locomotion.coyote_timer,
        local_player.locomotion.jump_buffer_timer,
        anim_state_name(local_player.anim_state), local_player.anim_blend,
        local_player.anim_phase, local_player_animation.transition_alpha(),
        player_anim_event_name(local_player.last_anim_event),
        local_player.procedural.spine_lean, local_player.procedural.turn_bank,
        local_player.procedural.landing_compression,
        local_player.procedural.jump_anticipation,
        k_vehicle_feature_enabled ? (vehicle.occupied ? "ONBOARD" : "ON FOOT")
                                  : "DISABLED",
        vehicle_distance, last_vehicle_control.throttle,
        last_vehicle_control.brake, last_vehicle_control.steer,
        last_vehicle_control.handbrake,
        k_vehicle_feature_enabled ? (aircraft.occupied ? "ONBOARD" : "ON FOOT")
                                  : "DISABLED",
        aircraft.speed, aircraft_distance, last_aircraft_control.throttle,
        last_aircraft_control.yaw, last_aircraft_control.pitch,
        last_aircraft_control.roll);
    snapshot.devhud_text = text;
  }

  return snapshot;
}

RuntimeDebugSceneSnapshot Engine::build_debug_scene_snapshot() const {
  RuntimeDebugSceneSnapshot snapshot{};
  snapshot.collision_debug_enabled = runtime_options.debug_collision;
  snapshot.debug_collision_only = runtime_options.debug_collision_only;
  snapshot.devhud_enabled = runtime_options.devhud;
  snapshot.splitscreen = runtime_options.splitscreen;
  snapshot.spherical_planet = runtime_options.spherical_planet;
  snapshot.render_skeleton_only =
      gui_menu.character() == GuiMenu::Character::Skeleton;
  if (gui_menu.character() == GuiMenu::Character::Humanoid &&
      has_humanoid_player_model) {
    snapshot.selected_player_model = &humanoid_player_model;
  }

  snapshot.collision_world = &collision_world;
  snapshot.local_player = &local_player;
  snapshot.local_player_animation = &local_player_animation;
  if (runtime_options.splitscreen) {
    snapshot.local_player_secondary = &local_player_secondary;
    snapshot.local_player_secondary_animation = &local_player_secondary_animation;
  }
  snapshot.last_collision_debug = last_collision_debug;
  snapshot.vehicle = RuntimeVehicleDebugSnapshot{
      .controller = &vehicle.controller,
      .position = vehicle.position,
      .yaw = vehicle.yaw,
      .occupied = vehicle.occupied,
  };
  snapshot.aircraft = RuntimeAircraftDebugSnapshot{
      .position = aircraft.position,
      .yaw = aircraft.yaw,
      .occupied = aircraft.occupied,
  };
  snapshot.spherical_planet_center = world_state.spherical_planet_center;
  snapshot.spherical_planet_radius = world_state.spherical_planet_radius;
  snapshot.extraction_unlocked = world_state.extraction_unlocked;
  snapshot.objective_round_complete = world_state.objective_round_complete;
  snapshot.extraction_zone_position = world_state.extraction_zone_position;
  snapshot.extraction_zone_radius = world_state.extraction_zone_radius;
  snapshot.active_minigame = active_minigame;
  snapshot.active_minigame_hotspot = active_minigame_hotspot;

  snapshot.minigame_hotspots.reserve(minigame_hotspots.size());
  for (size_t i = 0; i < minigame_hotspots.size(); ++i) {
    const MiniGameHotspot &hotspot = minigame_hotspots[i];
    snapshot.minigame_hotspots.push_back(RuntimeMiniGameHotspotDebugSnapshot{
        .type = hotspot.type,
        .position = hotspot.position,
        .nearby = static_cast<int>(i) == nearby_minigame_hotspot,
        .active = static_cast<int>(i) == active_minigame_hotspot,
    });
  }

  snapshot.objective_nodes.reserve(world_state.objective_nodes.size());
  for (size_t i = 0; i < world_state.objective_nodes.size(); ++i) {
    const auto &node = world_state.objective_nodes[i];
    snapshot.objective_nodes.push_back(RuntimeObjectiveDebugSnapshot{
        .position = node.position,
        .activated = node.activated,
        .nearby = static_cast<int>(i) == world_state.nearby_objective_node,
    });
  }

  snapshot.remote_players.reserve(remote_render_players.size());
  for (const auto &[player_id, render_player] : remote_render_players) {
    snapshot.remote_players.push_back(RuntimeRemoteDebugPlayerSnapshot{
        .player_id = player_id,
        .position = render_player.position,
        .orientation = render_player.orientation,
        .anim_state = render_player.anim_state,
        .anim_phase = render_player.anim_phase,
        .anim_blend = render_player.anim_blend,
        .animation_runtime = &render_player.animation_runtime,
    });
  }

  return snapshot;
}

void Engine::refresh_overlay_text() {
  hud_composer.compose(build_hud_snapshot(), render_stats, scene);
}

void Engine::rebuild_dynamic_debug_mesh() {
  scene.debug_world = debug_scene_builder.build(build_debug_scene_snapshot());
}
