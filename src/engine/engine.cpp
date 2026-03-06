#include "engine/engine.hpp"

#include "engine_gameplay/animation/skeletal_animator.hpp"
#include "engine_gameplay/player/player_controller.hpp"
#include "engine_gameplay/player/player_visuals.hpp"
#include "engine_net/remote_interp.hpp"
#include "engine_physics/avbd_solver.hpp"
#include "engine_render/debug_draw/debug_draw.hpp"
#include "engine_render/debug_text.hpp"
#include "engine_world/world_gen.hpp"

#include <spdlog/spdlog.h>

#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
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
constexpr bool k_vehicle_feature_enabled = false;
constexpr float k_vehicle_visual_yaw_offset = 0.0f;
constexpr float k_aircraft_interact_radius = 4.2f;
constexpr float k_aircraft_body_length = 2.7f;
constexpr float k_aircraft_body_width = 1.1f;
constexpr float k_aircraft_body_height = 0.55f;
constexpr uint32_t k_remote_interp_delay_ticks = 6;
constexpr size_t k_remote_sample_history_max = 16;
constexpr float k_minigame_interact_radius = 6.5f;

std::filesystem::path executable_directory() {
  namespace fs = std::filesystem;
#if defined(_WIN32)
  std::array<char, 4096> path{};
  const unsigned long len = GetModuleFileNameA(
      nullptr, path.data(), static_cast<unsigned long>(path.size()));
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

std::vector<std::string> candidate_model_paths(const char *subdir,
                                               const char *model_filename) {
  namespace fs = std::filesystem;
  std::vector<std::string> out;
  out.reserve(20);
  if (!subdir || *subdir == '\0' || !model_filename ||
      *model_filename == '\0') {
    return out;
  }
  const std::string rel =
      std::string("assets/models/") + subdir + "/" + model_filename;

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

glm::vec3 rotate_on_surface(const glm::vec3 &local, const SurfaceFrame &frame,
                            float yaw_radians) {
  glm::vec3 forward =
      frame.north * std::cos(yaw_radians) + frame.east * std::sin(yaw_radians);
  if (glm::length(forward) <= 0.001f) {
    forward = frame.north;
  } else {
    forward = glm::normalize(forward);
  }
  glm::vec3 right = glm::cross(forward, frame.up);
  if (glm::length(right) <= 0.001f) {
    right = frame.east;
  } else {
    right = glm::normalize(right);
  }
  return right * local.x + frame.up * local.y + forward * local.z;
}

glm::vec3 player_color_from_id(uint32_t player_id) {
  return player_color_from_network_id(player_id);
}

std::string net_fixed_string(const char *data, size_t size) {
  if (!data || size == 0) {
    return std::string();
  }
  size_t len = 0;
  while (len < size && data[len] != '\0') {
    ++len;
  }
  return std::string(data, len);
}

std::string net_session_status_line(const NetSessionInfo &info) {
  std::string name =
      net_fixed_string(info.server_name, sizeof(info.server_name));
  if (name.empty()) {
    name = "VOXOV Session";
  }
  const std::string mode =
      net_session_flag_set(info.flags, NetSessionFlags::LoopbackOnly) ? "LOCAL"
                                                                      : "WI-FI";
  return name + " [" + std::to_string(info.current_players) + "/" +
         std::to_string(info.max_players) + "] " + mode;
}

constexpr uint64_t k_hash_offset = 1469598103934665603ull;
constexpr uint64_t k_hash_prime = 1099511628211ull;

void hash_bytes(uint64_t &hash, const void *data, size_t size) {
  const uint8_t *bytes = static_cast<const uint8_t *>(data);
  for (size_t i = 0; i < size; ++i) {
    hash ^= static_cast<uint64_t>(bytes[i]);
    hash *= k_hash_prime;
  }
}

template <typename T> void hash_value(uint64_t &hash, const T &value) {
  hash_bytes(hash, &value, sizeof(T));
}

void hash_string(uint64_t &hash, const std::string &value) {
  const size_t len = value.size();
  hash_value(hash, len);
  if (!value.empty()) {
    hash_bytes(hash, value.data(), value.size());
  }
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

struct AnimatedCapsuleShape {
  float radius = 0.35f;
  float height = 1.8f;
  float bob = 0.0f;
  float pivot_height = 1.5f;
};

float anim_pulse(float phase) { return std::fabs(std::sin(phase)); }

glm::quat facing_from_velocity(glm::vec3 velocity, const glm::quat &fallback) {
  const glm::vec2 flat(velocity.x, velocity.z);
  const float speed = glm::length(flat);
  if (speed < 0.08f) {
    return fallback;
  }
  const float yaw = std::atan2(velocity.x, velocity.z);
  return glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f));
}

AnimatedCapsuleShape animated_shape(uint8_t anim_state, float anim_phase,
                                    float anim_blend, float base_radius,
                                    float base_height,
                                    float base_pivot_height) {
  AnimatedCapsuleShape out{};
  out.radius = base_radius;
  out.height = base_height;
  out.pivot_height = base_pivot_height;
  out.bob = 0.01f * std::sin(anim_phase);

  switch (anim_state) {
  case static_cast<uint8_t>(PlayerAnimState::StartMove):
  case static_cast<uint8_t>(PlayerAnimState::LocomotionWalk):
  case static_cast<uint8_t>(PlayerAnimState::PivotLeft):
  case static_cast<uint8_t>(PlayerAnimState::PivotRight):
  case static_cast<uint8_t>(PlayerAnimState::TurnInPlaceLeft):
  case static_cast<uint8_t>(PlayerAnimState::TurnInPlaceRight):
  case static_cast<uint8_t>(PlayerAnimState::MovingTurn):
    out.bob = 0.06f * std::max(0.35f, anim_blend) * anim_pulse(anim_phase);
    break;
  case static_cast<uint8_t>(PlayerAnimState::LocomotionRun):
    out.bob = 0.11f * std::max(0.55f, anim_blend) * anim_pulse(anim_phase);
    break;
  case static_cast<uint8_t>(PlayerAnimState::JumpTakeoff):
  case static_cast<uint8_t>(PlayerAnimState::JumpLoop):
  case static_cast<uint8_t>(PlayerAnimState::FallLoop):
  case static_cast<uint8_t>(PlayerAnimState::LandSoft):
  case static_cast<uint8_t>(PlayerAnimState::LandHard):
    out.bob = 0.08f * std::sin(anim_phase * 0.65f);
    break;
  case static_cast<uint8_t>(PlayerAnimState::StopMove):
  case static_cast<uint8_t>(PlayerAnimState::Recovery):
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

bool try_load_character_model(SkinnedModel &model, const char *label,
                              const char *filename, bool &out_loaded) {
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

glm::ivec2 tetris_visual_cell(int shape, int rot, int i) {
  static constexpr glm::ivec2 k_shape_rot[4][4][4] = {
      {
          // I
          {{-1, 0}, {0, 0}, {1, 0}, {2, 0}},
          {{0, -1}, {0, 0}, {0, 1}, {0, 2}},
          {{-1, 1}, {0, 1}, {1, 1}, {2, 1}},
          {{1, -1}, {1, 0}, {1, 1}, {1, 2}},
      },
      {
          // O
          {{0, 0}, {1, 0}, {0, 1}, {1, 1}},
          {{0, 0}, {1, 0}, {0, 1}, {1, 1}},
          {{0, 0}, {1, 0}, {0, 1}, {1, 1}},
          {{0, 0}, {1, 0}, {0, 1}, {1, 1}},
      },
      {
          // T
          {{-1, 0}, {0, 0}, {1, 0}, {0, 1}},
          {{0, -1}, {0, 0}, {0, 1}, {1, 0}},
          {{-1, 0}, {0, 0}, {1, 0}, {0, -1}},
          {{0, -1}, {0, 0}, {0, 1}, {-1, 0}},
      },
      {
          // L
          {{-1, 0}, {0, 0}, {1, 0}, {1, 1}},
          {{0, -1}, {0, 0}, {0, 1}, {1, -1}},
          {{-1, -1}, {-1, 0}, {0, 0}, {1, 0}},
          {{-1, 1}, {0, -1}, {0, 0}, {0, 1}},
      }};
  return k_shape_rot[shape % 4][rot % 4][i % 4];
}
} // namespace

void Engine::init(void *window_handle, RenderBackendType backend_type,
                  const EngineRuntimeOptions &options) {
  runtime_options = options;
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
  local_player = PlayerControllerSystem::spawn_player(collision_world);
  if (runtime_options.spherical_planet && spherical_planet_radius > 0.0f) {
    const float spawn_radius =
        spherical_planet_radius + local_player.controller.capsuleHeight * 0.52f;
    local_player.transform.position =
        spherical_planet_center + glm::vec3(0.0f, spawn_radius, 0.0f);
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
  if (runtime_options.spherical_planet && spherical_planet_radius > 0.0f) {
    const glm::vec3 spawn_up = glm::normalize(local_player.transform.position -
                                              spherical_planet_center);
    const SurfaceFrame spawn_frame = make_surface_frame(spawn_up);
    const glm::vec3 vehicle_seed = local_player.transform.position +
                                   spawn_frame.east * 3.5f +
                                   spawn_frame.north * 1.5f;
    const glm::vec3 vehicle_dir =
        glm::normalize(vehicle_seed - spherical_planet_center);
    vehicle.position = spherical_planet_center +
                       vehicle_dir * (spherical_planet_radius +
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
  if (runtime_options.spherical_planet && spherical_planet_radius > 0.0f) {
    const glm::vec3 spawn_up = glm::normalize(local_player.transform.position -
                                              spherical_planet_center);
    const SurfaceFrame spawn_frame = make_surface_frame(spawn_up);
    const glm::vec3 aircraft_seed = local_player.transform.position -
                                    spawn_frame.east * 5.0f -
                                    spawn_frame.north * 4.0f;
    const glm::vec3 aircraft_dir =
        glm::normalize(aircraft_seed - spherical_planet_center);
    aircraft.position = spherical_planet_center +
                        aircraft_dir * (spherical_planet_radius + 1.8f);
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

  try_load_character_model(humanoid_player_model, "humanoid", "CesiumMan.glb",
                           has_humanoid_player_model);

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
  has_snapshot = false;
  latest_snapshot = NetSnapshot{};
  remote_players.clear();
  remote_render_players.clear();
  local_player.network_id = 1;
  local_replication.network_id = 1;
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
  GuiMenuActions menu_actions{};
  gui_menu.handle_input(primary_input, runtime_options.devhud,
                        runtime_options.noclip, menu_actions);
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
  if (menu_actions.leave_session) {
    leave_session();
  }
  if (menu_actions.host_local) {
    gameplay_started = true;
    leave_session();
    start_local_server(7777, true);
    connect("127.0.0.1", 7777);
    lan_discovery.stop();
    searching_nearby = false;
    multiplayer_hint = "Hosting this device only.";
  }
  if (menu_actions.host_lan) {
    gameplay_started = true;
    leave_session();
    start_local_server(7777, false);
    connect("127.0.0.1", 7777);
    lan_discovery.start_host(7777, "VOXOV Host");
    searching_nearby = false;
    multiplayer_hint =
        "Hosting Wi-Fi game. Tell friends: Multiplayer > Join Nearby.";
  }
  if (menu_actions.join_local || menu_actions.join_nearby) {
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
  }
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

GuiSessionContext Engine::gui_session_context() const {
  GuiSessionContext session{};
  const NetClientConnectionState connection_state =
      net_client.connection_state();
  session.connected = connection_state == NetClientConnectionState::Connected;
  session.connecting = connection_state == NetClientConnectionState::Connecting;
  session.searching = searching_nearby;
  session.hosting_local = local_server_running && local_server_loopback;
  session.hosting_lan = local_server_running && !local_server_loopback;
  session.can_leave = session.connected || session.connecting ||
                      session.searching || session.hosting_local ||
                      session.hosting_lan;
  session.status = multiplayer_status_text();
  return session;
}

std::string Engine::multiplayer_status_text() const {
  const NetClientConnectionState connection_state =
      net_client.connection_state();
  if (connection_state == NetClientConnectionState::Connected) {
    if (net_client.has_session_info()) {
      return net_session_status_line(net_client.session_info());
    }
    return "Connected to game server.";
  }
  if (searching_nearby) {
    return "Searching nearby Wi-Fi hosts...";
  }
  if (connection_state == NetClientConnectionState::Connecting) {
    const std::string &target_host = net_client.connect_target_host();
    if (!target_host.empty()) {
      return "Connecting to " + target_host + ":" +
             std::to_string(net_client.connect_target_port()) + "...";
    }
    return "Connecting...";
  }
  if (local_server_running) {
    return local_server_loopback ? "Hosting this device only."
                                 : "Hosting Wi-Fi game.";
  }
  return multiplayer_hint;
}

void Engine::update_remote_interpolation(double frame_dt) {
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

  PlayerControllerSystem::update_camera_rig(local_player, gameplay_input,
                                            touch_input_mode,
                                            static_cast<float>(frame_dt));
  if (runtime_options.splitscreen) {
    PlayerControllerSystem::update_camera_rig(local_player_secondary,
                                              gameplay_input_secondary, false,
                                              static_cast<float>(frame_dt));
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
    } else if (runtime_options.spherical_planet &&
               spherical_planet_radius > 0.0f) {
      update_spherical_player_sim(step_input,
                                  static_cast<float>(fixed.fixed_dt));
      last_collision_debug = PlayerCollisionDebug{};
    } else {
      last_collision_debug = PlayerControllerSystem::simulate_fixed(
          local_player, step_input, collision_world,
          static_cast<float>(fixed.fixed_dt), runtime_options.noclip);
    }

    if (runtime_options.splitscreen) {
      local_player_secondary_prev_position =
          local_player_secondary.transform.position;
      InputState step_input_secondary = gameplay_input_secondary;
      last_collision_debug_secondary = PlayerControllerSystem::simulate_fixed(
          local_player_secondary, step_input_secondary, collision_world,
          static_cast<float>(fixed.fixed_dt), runtime_options.noclip);
    }
    sync_local_animation_runtime(local_player, local_player_animation,
                                 static_cast<float>(fixed.fixed_dt));
    if (runtime_options.splitscreen) {
      sync_local_animation_runtime(local_player_secondary,
                                   local_player_secondary_animation,
                                   static_cast<float>(fixed.fixed_dt));
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
    render_stats.cpu_ms =
        (fps_accumulator * 1000.0) / static_cast<double>(fps_frames);
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
  ctx.debug_xray =
      runtime_options.debug_collision && runtime_options.debug_xray;
  renderer.begin_frame(ctx, render_stats);
  renderer.end_frame();
}

const RenderStats &Engine::stats() const { return render_stats; }

void Engine::build_static_scene() {
  if (runtime_options.spherical_planet) {
    world_chunk.generate_spherical_planet_seeded(k_voxov_flat_world_seed);
    spherical_planet_center =
        glm::vec3(static_cast<float>(VoxelChunk::CHUNK_X - 1) * 0.5f,
                  static_cast<float>(VoxelChunk::CHUNK_Y - 1) * 0.42f,
                  static_cast<float>(VoxelChunk::CHUNK_Z - 1) * 0.5f);
    spherical_planet_radius =
        static_cast<float>(std::min(
            {VoxelChunk::CHUNK_X, VoxelChunk::CHUNK_Y, VoxelChunk::CHUNK_Z})) *
        0.34f;
  } else {
    generate_flat_world_locomotion_chunk(world_chunk);
    spherical_planet_center = glm::vec3(0.0f);
    spherical_planet_radius = 0.0f;
  }
  collision_world = VoxelCollisionWorld(&world_chunk);

  scene = RenderScene{};
  scene.opaque_meshes.push_back(world_chunk.build_sky_placeholder(240.0f));
  if (runtime_options.spherical_planet) {
    scene.opaque_meshes.push_back(world_chunk.build_naive_mesh());
  } else {
    constexpr int k_render_chunk_radius = 1;
    for (int chunk_z = -k_render_chunk_radius; chunk_z <= k_render_chunk_radius;
         ++chunk_z) {
      for (int chunk_x = -k_render_chunk_radius;
           chunk_x <= k_render_chunk_radius; ++chunk_x) {
        VoxelChunk render_chunk{};
        generate_flat_world_locomotion_chunk(
            render_chunk, k_voxov_flat_world_seed, chunk_x, chunk_z);
        const glm::vec3 chunk_origin(
            static_cast<float>(chunk_x * VoxelChunk::CHUNK_X), 0.0f,
            static_cast<float>(chunk_z * VoxelChunk::CHUNK_Z));
        scene.opaque_meshes.push_back(
            render_chunk.build_naive_mesh(chunk_origin));
      }
    }
  }
  scene.debug_grid = world_chunk.build_debug_grid(160.0f, 1.0f);

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
          spherical_planet_center + dir * (spherical_planet_radius * 2.6f);
      float hit_dist = 0.0f;
      glm::vec3 position =
          spherical_planet_center + dir * (spherical_planet_radius + 0.12f);
      if (collision_world.raycast(ray_start, -dir,
                                  spherical_planet_radius * 3.2f, hit_dist)) {
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
  if (runtime_options.spherical_planet && spherical_planet_radius > 0.0f) {
    const glm::vec3 to_player = render_position - spherical_planet_center;
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
  if (runtime_options.spherical_planet && spherical_planet_radius > 0.0f) {
    const glm::vec3 up =
        glm::normalize(vehicle.position - spherical_planet_center);
    return vehicle.position + up * (k_vehicle_body_height + 0.5f);
  }
  return vehicle.position +
         rotate_y(glm::vec3(0.0f, k_vehicle_body_height + 0.5f, 0.0f),
                  vehicle.yaw + k_vehicle_visual_yaw_offset);
}

glm::vec3 Engine::aircraft_seat_world_position() const {
  if (runtime_options.spherical_planet && spherical_planet_radius > 0.0f) {
    const glm::vec3 up =
        glm::normalize(aircraft.position - spherical_planet_center);
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
  if (!input.interact_pressed || gui_menu.open() || !gameplay_started) {
    return;
  }
  if (aircraft.occupied) {
    return;
  }

  if (vehicle.occupied) {
    vehicle.occupied = false;
    if (runtime_options.spherical_planet && spherical_planet_radius > 0.0f) {
      const glm::vec3 up =
          glm::normalize(vehicle.position - spherical_planet_center);
      const SurfaceFrame frame = make_surface_frame(up);
      glm::vec3 exit_candidate = vehicle.position - frame.east * 1.8f;
      glm::vec3 exit_dir =
          glm::normalize(exit_candidate - spherical_planet_center);
      const float shell_radius = spherical_planet_radius +
                                 local_player.controller.capsuleHeight * 0.52f;
      local_player.transform.position =
          spherical_planet_center + exit_dir * shell_radius;
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
  if (!input.interact_pressed || gui_menu.open() || !gameplay_started) {
    return;
  }
  if (vehicle.occupied) {
    return;
  }

  if (aircraft.occupied) {
    aircraft.occupied = false;
    aircraft.throttle_cmd = 0.0f;
    if (runtime_options.spherical_planet && spherical_planet_radius > 0.0f) {
      const glm::vec3 up =
          glm::normalize(aircraft.position - spherical_planet_center);
      const SurfaceFrame frame = make_surface_frame(up);
      glm::vec3 exit_candidate =
          aircraft.position - frame.east * 2.4f - frame.north * 0.9f;
      glm::vec3 exit_dir =
          glm::normalize(exit_candidate - spherical_planet_center);
      const float shell_radius = spherical_planet_radius +
                                 local_player.controller.capsuleHeight * 0.52f;
      local_player.transform.position =
          spherical_planet_center + exit_dir * shell_radius;
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

void Engine::handle_minigame_interaction(const InputState &input) {
  nearby_minigame_hotspot = -1;
  minigame_hint.clear();

  if (!gameplay_started || gui_menu.open()) {
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
  if (runtime_options.spherical_planet && spherical_planet_radius > 0.0f) {
    const glm::vec3 up =
        glm::normalize(hotspot.position - spherical_planet_center);
    const float shell_radius =
        spherical_planet_radius + local_player.controller.capsuleHeight * 0.52f;
    local_player.transform.position =
        spherical_planet_center + up * shell_radius;
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
  if (dt <= 0.0f || spherical_planet_radius <= 0.0f) {
    return;
  }
  const bool was_grounded = local_player.controller.grounded;

  const glm::vec3 to_player =
      local_player.transform.position - spherical_planet_center;
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
      local_player.transform.position - spherical_planet_center;
  const float updated_dist = std::max(glm::length(to_updated), 0.001f);
  const glm::vec3 updated_up = to_updated / updated_dist;
  const float shell_radius =
      spherical_planet_radius + local_player.controller.capsuleHeight * 0.52f;
  if (updated_dist < shell_radius + 0.08f) {
    local_player.transform.position =
        spherical_planet_center + updated_up * shell_radius;
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

void Engine::refresh_overlay_text() {
  const GuiMenuView menu_view = gui_menu.build_view(
      runtime_options.devhud, runtime_options.noclip, gui_session_context());
  render_stats.menu_open = menu_view.open;
  render_stats.menu_selected = menu_view.selected;
  render_stats.menu_title = menu_view.title;
  render_stats.menu_items = menu_view.items;
  render_stats.menu_guide = menu_view.guide_lines;
  render_stats.menu_status = menu_view.status;
  render_stats.menu_text.clear();

  std::string minigame_title;
  std::string minigame_status;
  std::string minigame_objective;
  std::string minigame_controls;
  std::string hotspot_text;
  float minigame_progress = 0.0f;

  if (!menu_view.open &&
      (active_minigame.active || nearby_minigame_hotspot >= 0)) {
    if (active_minigame.active) {
      minigame_title = minigame_name(active_minigame.type);
      minigame_status = minigame_status_text(active_minigame);
      minigame_objective = "OBJECTIVE";
      minigame_controls = "WASD move  SPACE/F/E action  C/CTRL exit";
      minigame_progress = 0.0f;

      switch (active_minigame.type) {
      case MiniGameType::Snake:
        minigame_objective = "OBJECTIVE: Reach length 24 without colliding";
        minigame_controls = "WASD steer snake  C/CTRL exit";
        minigame_progress =
            std::clamp(static_cast<float>(active_minigame.snake.length) / 24.0f,
                       0.0f, 1.0f);
        break;
      case MiniGameType::Golf: {
        minigame_objective = "OBJECTIVE: Sink the ball in fewer strokes";
        minigame_controls = "A/D aim  W/S power  SPACE/F/E swing  C/CTRL exit";
        const float dist =
            glm::length(active_minigame.golf.hole - active_minigame.golf.ball);
        minigame_progress = std::clamp(1.0f - (dist / 9.5f), 0.0f, 1.0f);
        break;
      }
      case MiniGameType::Tetris:
        minigame_objective = "OBJECTIVE: Reach score 1200 before topping out";
        minigame_controls = "A/D move  SPACE rotate  S soft drop  C/CTRL exit";
        minigame_progress = std::clamp(
            static_cast<float>(active_minigame.score) / 1200.0f, 0.0f, 1.0f);
        break;
      case MiniGameType::Racing:
        minigame_objective = "OBJECTIVE: Complete 3 laps";
        minigame_controls = "W/S throttle  A/D steer  C/CTRL exit";
        minigame_progress =
            std::clamp((static_cast<float>(active_minigame.racing.lap) +
                        active_minigame.racing.track_progress / 65.0f) /
                           3.0f,
                       0.0f, 1.0f);
        break;
      case MiniGameType::TicTacToe:
        minigame_objective = "OBJECTIVE: Align 3 marks before the AI";
        minigame_controls = "WASD move cursor  SPACE/F/E place  C/CTRL exit";
        minigame_progress = std::clamp(
            static_cast<float>(active_minigame.tictactoe.turns) / 9.0f, 0.0f,
            1.0f);
        break;
      default:
        break;
      }

      if (active_minigame.completed) {
        minigame_controls = "Press F/E to leave";
        minigame_progress = 1.0f;
      }
    } else {
      hotspot_text = "MINIGAME HOTSPOT";
      if (!minigame_hint.empty()) {
        hotspot_text += "\n";
        hotspot_text += minigame_hint;
      }
      hotspot_text += "\nPress F or E to start";
    }
  }

  uint64_t overlay_state_hash = k_hash_offset;
  hash_value(overlay_state_hash, menu_view.open);
  hash_value(overlay_state_hash, menu_view.selected);
  hash_value(overlay_state_hash, runtime_options.devhud);
  hash_value(overlay_state_hash, runtime_options.noclip);
  hash_value(overlay_state_hash, nearby_minigame_hotspot);
  hash_value(overlay_state_hash, active_minigame.active);
  hash_value(overlay_state_hash, active_minigame.completed);
  hash_value(overlay_state_hash, active_minigame.type);
  hash_value(overlay_state_hash, minigame_progress);
  hash_string(overlay_state_hash, menu_view.title);
  hash_string(overlay_state_hash, menu_view.status);
  for (const std::string &line : menu_view.items) {
    hash_string(overlay_state_hash, line);
  }
  for (const std::string &line : menu_view.guide_lines) {
    hash_string(overlay_state_hash, line);
  }
  hash_string(overlay_state_hash, minigame_hint);
  hash_string(overlay_state_hash, minigame_title);
  hash_string(overlay_state_hash, minigame_status);
  hash_string(overlay_state_hash, minigame_objective);
  hash_string(overlay_state_hash, minigame_controls);
  hash_string(overlay_state_hash, hotspot_text);

  if (!runtime_options.devhud && has_overlay_state_hash &&
      overlay_state_hash == last_overlay_state_hash) {
    return;
  }

  scene.debug_screen = RenderMesh{};
  last_overlay_state_hash = overlay_state_hash;
  has_overlay_state_hash = true;

  auto append_screen_rect = [&](float x0, float y0, float x1, float y1,
                                const glm::vec3 &color) {
    RenderMesh rect{};
    const uint32_t base = 0;
    rect.vertices.push_back({glm::vec3(x0, y0, 0.0f), color});
    rect.vertices.push_back({glm::vec3(x1, y0, 0.0f), color});
    rect.vertices.push_back({glm::vec3(x1, y1, 0.0f), color});
    rect.vertices.push_back({glm::vec3(x0, y1, 0.0f), color});
    rect.indices.insert(rect.indices.end(),
                        {base, base + 1, base + 2, base, base + 2, base + 3});
    append_mesh(scene.debug_screen, rect);
  };

  const float safe_left = -0.92f;
  const float safe_right = 0.92f;
  const float safe_top = 0.92f;
  const float safe_bottom = -0.90f;

  struct ScreenPanel {
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
  };

  auto draw_panel = [&](const ScreenPanel &panel, const glm::vec3 &outer,
                        const glm::vec3 &inner) {
    append_screen_rect(panel.x0, panel.y0, panel.x1, panel.y1, outer);
    append_screen_rect(panel.x0 + 0.01f, panel.y0 - 0.01f, panel.x1 - 0.01f,
                       panel.y1 + 0.01f, inner);
  };

  const ScreenPanel menu_panel{safe_left - 0.02f, safe_top, 0.18f,
                               safe_bottom + 0.12f};
  const ScreenPanel devhud_panel{safe_left - 0.02f, safe_top, 0.14f, 0.12f};
  const ScreenPanel minigame_panel{0.30f, safe_top, safe_right, 0.70f};
  const ScreenPanel hotspot_panel{0.46f, -0.73f, safe_right, safe_bottom};

  if (menu_view.open) {
    draw_panel(menu_panel, glm::vec3(0.05f, 0.07f, 0.10f),
               glm::vec3(0.09f, 0.11f, 0.16f));

    float y = menu_panel.y0 - 0.06f;
    if (!menu_view.title.empty()) {
      append_mesh(scene.debug_screen,
                  build_screen_text_mesh(menu_view.title, menu_panel.x0 + 0.03f,
                                         y, 0.0082f,
                                         glm::vec3(0.96f, 0.98f, 1.0f)));
      y -= 0.11f;
    }

    for (size_t i = 0; i < menu_view.items.size(); ++i) {
      const bool selected = static_cast<int>(i) == menu_view.selected;
      std::string line =
          selected ? ("> " + menu_view.items[i]) : ("  " + menu_view.items[i]);
      append_mesh(
          scene.debug_screen,
          build_screen_text_mesh(line, menu_panel.x0 + 0.05f, y, 0.0069f,
                                 selected ? glm::vec3(0.96f, 0.98f, 1.0f)
                                          : glm::vec3(0.86f, 0.91f, 0.98f)));
      y -= 0.095f;
    }

    if (!menu_view.guide_lines.empty()) {
      y -= 0.02f;
      for (const std::string &line : menu_view.guide_lines) {
        append_mesh(scene.debug_screen,
                    build_screen_text_mesh(line, menu_panel.x0 + 0.05f, y,
                                           0.0059f,
                                           glm::vec3(0.80f, 0.88f, 0.97f)));
        y -= 0.072f;
      }
    }

    if (!menu_view.status.empty()) {
      append_mesh(scene.debug_screen,
                  build_screen_text_mesh("STATUS: " + menu_view.status,
                                         menu_panel.x0 + 0.03f,
                                         menu_panel.y1 + 0.05f, 0.0056f,
                                         glm::vec3(0.88f, 0.93f, 0.99f)));
    }
  } else if (runtime_options.devhud) {
    char text[1024]{};
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
        "FPS %.1f DT %.3f FIX %.3f\nP %.1f %.1f %.1f V %.1f %.1f %.1f G "
        "%d\nPEN %.3f N %.1f %.1f %.1f\nYAW %.1f PIT %.1f LOOK %.1f %.1f\nRMB "
        "%d LOCK %d LKEN %d REM %d\nNET C%d LID %u\nNCL tx/rx pps %u/%u Bps "
        "%u/%u inv %llu\nNSV on%d tx/rx pps %u/%u Bps %u/%u snap %u pst "
        "%u\nREC %s err %.2f tick %u seq %u replay %u corr %llu\nLOCO %s SPD "
        "%.2f GND %d SLP %.1f CYO %.2f BUF %.2f\nANIM %s BL %.2f PH %.2f X "
        "%.2f EVT %s\nPROC lean %.2f bank %.2f land %.2f jump %.2f\nVEH %s "
        "DIST %.1f C[th %.2f br %.2f st %.2f hb %.2f]\nAIR %s SPD %.1f DIST "
        "%.1f C[th %.2f y %.2f p %.2f r %.2f]",
        render_stats.fps, last_frame_dt, fixed.fixed_dt,
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
    draw_panel(devhud_panel, glm::vec3(0.05f, 0.07f, 0.10f),
               glm::vec3(0.09f, 0.11f, 0.16f));
    append_mesh(scene.debug_screen,
                build_screen_text_mesh(text, devhud_panel.x0 + 0.03f,
                                       devhud_panel.y0 - 0.06f, 0.0049f,
                                       glm::vec3(0.95f, 0.95f, 0.82f)));
  }

  if (!menu_view.open &&
      (active_minigame.active || nearby_minigame_hotspot >= 0)) {
    if (active_minigame.active) {
      draw_panel(minigame_panel, glm::vec3(0.04f, 0.06f, 0.08f),
                 glm::vec3(0.08f, 0.10f, 0.13f));

      append_mesh(scene.debug_screen,
                  build_screen_text_mesh(minigame_title,
                                         minigame_panel.x0 + 0.04f,
                                         minigame_panel.y0 - 0.05f, 0.0068f,
                                         glm::vec3(0.98f, 0.98f, 1.0f)));
      append_mesh(scene.debug_screen,
                  build_screen_text_mesh(minigame_status,
                                         minigame_panel.x0 + 0.04f,
                                         minigame_panel.y0 - 0.10f, 0.0052f,
                                         glm::vec3(0.89f, 0.95f, 1.0f)));
      append_mesh(scene.debug_screen,
                  build_screen_text_mesh(minigame_objective,
                                         minigame_panel.x0 + 0.04f,
                                         minigame_panel.y0 - 0.15f, 0.0048f,
                                         glm::vec3(0.86f, 0.91f, 0.98f)));
      append_mesh(scene.debug_screen,
                  build_screen_text_mesh(minigame_controls,
                                         minigame_panel.x0 + 0.04f,
                                         minigame_panel.y0 - 0.21f, 0.0046f,
                                         glm::vec3(0.83f, 0.89f, 0.97f)));
      if (!minigame_hint.empty()) {
        append_mesh(scene.debug_screen,
                    build_screen_text_mesh(minigame_hint,
                                           minigame_panel.x0 + 0.04f,
                                           minigame_panel.y0 - 0.25f, 0.0045f,
                                           glm::vec3(0.8f, 0.88f, 0.96f)));
      }

      append_screen_rect(minigame_panel.x0 + 0.04f, 0.71f,
                         minigame_panel.x1 - 0.04f, 0.685f,
                         glm::vec3(0.18f, 0.20f, 0.24f));
      const float fill_right =
          (minigame_panel.x0 + 0.04f) +
          ((minigame_panel.x1 - 0.04f) - (minigame_panel.x0 + 0.04f)) *
              minigame_progress;
      append_screen_rect(minigame_panel.x0 + 0.04f, 0.71f, fill_right, 0.685f,
                         glm::vec3(0.24f, 0.72f, 0.98f));
    } else {
      draw_panel(hotspot_panel, glm::vec3(0.04f, 0.06f, 0.08f),
                 glm::vec3(0.08f, 0.10f, 0.13f));
      append_mesh(scene.debug_screen,
                  build_screen_text_mesh(hotspot_text, hotspot_panel.x0 + 0.04f,
                                         hotspot_panel.y0 - 0.05f, 0.0050f,
                                         glm::vec3(0.91f, 0.96f, 1.0f)));
    }
  }
}

void Engine::rebuild_dynamic_debug_mesh() {
  scene.debug_world = RenderMesh{};
  const bool collision_debug_enabled = runtime_options.debug_collision;
  const bool render_skeleton_only =
      gui_menu.character() == GuiMenu::Character::Skeleton;
  const SkinnedModel *selected_player_model = nullptr;
  if (gui_menu.character() == GuiMenu::Character::Humanoid &&
      has_humanoid_player_model) {
    selected_player_model = &humanoid_player_model;
  }
  const bool render_skinned_avatar =
      !render_skeleton_only && selected_player_model != nullptr;

  const AnimatedCapsuleShape local_shape = animated_shape(
      static_cast<uint8_t>(local_player.anim_state), local_player.anim_phase,
      local_player.anim_blend, local_player.controller.capsuleRadius,
      local_player.controller.capsuleHeight,
      local_player.camera_rig.pivotHeight);

  RenderMesh player_capsule = build_debug_capsule_mesh(
      local_player.transform.position, local_shape.radius, local_shape.height,
      player_color_from_id(local_player.network_id));

  RenderMesh target_marker = build_debug_sphere_mesh(
      local_player.transform.position +
          glm::vec3(0.0f, local_shape.pivot_height, 0.0f),
      0.12f, glm::vec3(0.2f, 0.85f, 1.0f));

  auto append_vehicle_tri = [&](const glm::vec3 &a, const glm::vec3 &b,
                                const glm::vec3 &c, const glm::vec3 &color) {
    const uint32_t base =
        static_cast<uint32_t>(scene.debug_world.vertices.size());
    scene.debug_world.vertices.push_back({a, color});
    scene.debug_world.vertices.push_back({b, color});
    scene.debug_world.vertices.push_back({c, color});
    scene.debug_world.indices.push_back(base + 0);
    scene.debug_world.indices.push_back(base + 1);
    scene.debug_world.indices.push_back(base + 2);
  };
  auto append_vehicle_quad = [&](const glm::vec3 &a, const glm::vec3 &b,
                                 const glm::vec3 &c, const glm::vec3 &d,
                                 const glm::vec3 &color) {
    append_vehicle_tri(a, b, c, color);
    append_vehicle_tri(a, c, d, color);
  };
  auto append_vehicle_box = [&](const glm::vec3 &local_center,
                                const glm::vec3 &half_extent,
                                const glm::vec3 &color) {
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
    append_vehicle_box(glm::vec3(0.0f, k_vehicle_body_height * 0.5f, 0.0f),
                       glm::vec3(k_vehicle_body_half_width,
                                 k_vehicle_body_height * 0.5f,
                                 k_vehicle_body_half_length),
                       vehicle.occupied ? glm::vec3(0.15f, 0.78f, 0.35f)
                                        : glm::vec3(0.85f, 0.62f, 0.22f));
    append_vehicle_box(glm::vec3(0.0f, k_vehicle_body_height + 0.28f, -0.1f),
                       glm::vec3(0.58f, 0.28f, 0.68f),
                       glm::vec3(0.2f, 0.35f, 0.42f));
    append_vehicle_box(
        glm::vec3(0.0f, 0.58f, k_vehicle_body_half_length - 0.22f),
        glm::vec3(0.55f, 0.12f, 0.14f), glm::vec3(0.08f, 0.08f, 0.08f));

    const auto &wheel_setup = vehicle.controller.wheel_setup();
    const float visual_yaw = vehicle.yaw + k_vehicle_visual_yaw_offset;
    const auto &wheel_compression =
        vehicle.controller.state().wheel_compression;
    for (size_t i = 0; i < wheel_setup.size(); ++i) {
      const GroundVehicleWheel &wheel = wheel_setup[i];
      const float suspension_length =
          wheel.suspension_rest_length -
          wheel_compression[i] * wheel.suspension_travel;
      const glm::vec3 wheel_offset(wheel.local_mount.x,
                                   wheel.local_mount.y - suspension_length,
                                   wheel.local_mount.z);
      append_mesh(scene.debug_world,
                  build_debug_sphere_mesh(
                      vehicle.position + rotate_y(wheel_offset, visual_yaw),
                      wheel.radius, glm::vec3(0.12f, 0.12f, 0.12f)));
    }

    append_mesh(
        scene.debug_world,
        build_debug_line_mesh(vehicle.position + glm::vec3(0.0f, 0.2f, 0.0f),
                              vehicle.position + glm::vec3(0.0f, 4.2f, 0.0f),
                              0.06f, glm::vec3(1.0f, 0.25f, 0.9f)));
    append_mesh(
        scene.debug_world,
        build_debug_sphere_mesh(vehicle.position + glm::vec3(0.0f, 4.35f, 0.0f),
                                0.22f, glm::vec3(1.0f, 0.25f, 0.9f)));

    const glm::vec3 aircraft_color = aircraft.occupied
                                         ? glm::vec3(0.12f, 0.84f, 0.95f)
                                         : glm::vec3(0.42f, 0.70f, 0.95f);
    auto append_aircraft_box = [&](const glm::vec3 &local_center,
                                   const glm::vec3 &half_extent) {
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

    append_aircraft_box(glm::vec3(0.0f, k_aircraft_body_height, 0.0f),
                        glm::vec3(k_aircraft_body_width * 0.5f,
                                  k_aircraft_body_height,
                                  k_aircraft_body_length * 0.5f));
    append_aircraft_box(glm::vec3(0.0f, k_aircraft_body_height + 0.1f, -0.2f),
                        glm::vec3(2.1f, 0.08f, 0.36f));
    append_aircraft_box(glm::vec3(0.0f, k_aircraft_body_height + 0.5f, -1.0f),
                        glm::vec3(0.16f, 0.44f, 0.16f));

    append_mesh(scene.debug_world,
                build_debug_line_mesh(
                    aircraft.position,
                    aircraft.position +
                        rotate_y(glm::vec3(0.0f, 0.0f, 4.0f), aircraft.yaw),
                    0.05f, glm::vec3(0.2f, 0.9f, 1.0f)));
  }

  if (!runtime_options.debug_collision_only) {
    auto append_minigame_voxel = [&](const glm::vec3 &center,
                                     const glm::vec3 &half,
                                     const glm::vec3 &color) {
      append_mesh(scene.debug_world,
                  build_debug_aabb_mesh(center - half, center + half, color));
    };

    for (size_t i = 0; i < minigame_hotspots.size(); ++i) {
      const MiniGameHotspot &hotspot = minigame_hotspots[i];
      const glm::vec3 color = minigame_color(hotspot.type);
      const bool selected = static_cast<int>(i) == nearby_minigame_hotspot ||
                            static_cast<int>(i) == active_minigame_hotspot;
      glm::vec3 up(0.0f, 1.0f, 0.0f);
      if (runtime_options.spherical_planet && spherical_planet_radius > 0.0f) {
        up = glm::normalize(hotspot.position - spherical_planet_center);
      }
      append_mesh(scene.debug_world,
                  build_debug_line_mesh(hotspot.position + up * 0.2f,
                                        hotspot.position + up * 3.0f,
                                        selected ? 0.09f : 0.06f, color));
      append_mesh(scene.debug_world,
                  build_debug_sphere_mesh(hotspot.position + up * 3.2f,
                                          selected ? 0.28f : 0.2f, color));
      append_mesh(scene.debug_world,
                  build_debug_sphere_mesh(hotspot.position + up * 0.15f,
                                          selected ? 0.17f : 0.12f,
                                          color * glm::vec3(1.1f)));
    }

    if (active_minigame.active && active_minigame_hotspot >= 0 &&
        active_minigame_hotspot < static_cast<int>(minigame_hotspots.size())) {
      const float board_yaw =
          local_player.camera_rig.yaw * 0.01745329251994329577f;
      glm::vec3 board_origin =
          local_player.transform.position +
          rotate_y(glm::vec3(0.0f, 1.28f, 2.35f), board_yaw);
      SurfaceFrame board_frame{};
      bool use_surface_frame = false;
      if (runtime_options.spherical_planet && spherical_planet_radius > 0.0f) {
        const glm::vec3 up =
            local_player.transform.position - spherical_planet_center;
        board_frame = make_surface_frame(up);
        board_origin = local_player.transform.position +
                       rotate_on_surface(glm::vec3(0.0f, 1.28f, 2.35f),
                                         board_frame, board_yaw);
        use_surface_frame = true;
      }
      auto board_point = [&](const glm::vec3 &local) {
        if (use_surface_frame) {
          return board_origin +
                 rotate_on_surface(local, board_frame, board_yaw);
        }
        return board_origin + rotate_y(local, board_yaw);
      };
      auto append_board_voxel = [&](const glm::vec3 &local_center,
                                    const glm::vec3 &half,
                                    const glm::vec3 &color) {
        append_minigame_voxel(board_point(local_center), half, color);
      };

      // Cabinet-like play station in front of the player.
      append_board_voxel(glm::vec3(0.0f, -0.20f, 0.0f),
                         glm::vec3(1.20f, 0.07f, 0.92f),
                         glm::vec3(0.18f, 0.20f, 0.23f));
      append_board_voxel(glm::vec3(0.0f, -0.08f, 0.0f),
                         glm::vec3(1.14f, 0.03f, 0.84f),
                         glm::vec3(0.10f, 0.12f, 0.15f));
      append_board_voxel(glm::vec3(0.0f, 0.72f, -0.78f),
                         glm::vec3(1.16f, 0.78f, 0.05f),
                         glm::vec3(0.09f, 0.10f, 0.13f));

      if (active_minigame.type == MiniGameType::Snake) {
        const float cell = 0.18f;
        const glm::vec3 base(-0.80f, -0.12f, -0.70f);
        const glm::vec3 half(0.075f, 0.075f, 0.055f);
        for (int i = 0; i < active_minigame.snake.length; ++i) {
          const glm::ivec2 c =
              active_minigame.snake.body[static_cast<size_t>(i)];
          append_board_voxel(base + glm::vec3(c.x * cell, c.y * cell, 0.0f),
                             half, glm::vec3(0.2f, 0.9f, 0.3f));
        }
        append_board_voxel(base + glm::vec3(active_minigame.snake.food.x * cell,
                                            active_minigame.snake.food.y * cell,
                                            0.0f),
                           half, glm::vec3(0.95f, 0.25f, 0.2f));
      } else if (active_minigame.type == MiniGameType::TicTacToe) {
        const glm::vec3 base(-0.32f, 0.01f, -0.32f);
        const int cursor = std::clamp(active_minigame.tictactoe.cursor, 0, 8);
        for (int y = 0; y < 3; ++y) {
          for (int x = 0; x < 3; ++x) {
            const int idx = y * 3 + x;
            const uint8_t cell =
                active_minigame.tictactoe.board[static_cast<size_t>(idx)];
            const glm::vec3 cpos = base + glm::vec3(x * 0.32f, 0.0f, y * 0.32f);
            append_board_voxel(cpos, glm::vec3(0.11f, 0.03f, 0.11f),
                               glm::vec3(0.18f, 0.22f, 0.26f));
            if (!active_minigame.completed && idx == cursor) {
              append_board_voxel(cpos + glm::vec3(0.0f, 0.055f, 0.0f),
                                 glm::vec3(0.11f, 0.015f, 0.11f),
                                 glm::vec3(0.95f, 0.95f, 0.35f));
            }
            if (cell == 1) {
              append_board_voxel(cpos + glm::vec3(0.0f, 0.1f, 0.0f),
                                 glm::vec3(0.05f),
                                 glm::vec3(0.15f, 0.9f, 0.3f));
            } else if (cell == 2) {
              append_board_voxel(cpos + glm::vec3(0.0f, 0.1f, 0.0f),
                                 glm::vec3(0.05f), glm::vec3(0.9f, 0.2f, 0.2f));
            }
          }
        }
      } else if (active_minigame.type == MiniGameType::Golf) {
        const glm::vec3 base(-0.78f, 0.0f, -0.55f);
        const glm::vec3 half(0.07f, 0.07f, 0.07f);
        const glm::vec3 ball =
            base + glm::vec3(active_minigame.golf.ball.x * 0.16f, 0.0f,
                             active_minigame.golf.ball.y * 0.16f);
        const glm::vec3 hole =
            base + glm::vec3(active_minigame.golf.hole.x * 0.16f, 0.0f,
                             active_minigame.golf.hole.y * 0.16f);
        append_board_voxel(ball, half, glm::vec3(0.9f));
        append_board_voxel(hole, half, glm::vec3(0.2f, 0.6f, 1.0f));

        const glm::vec3 aim_dir(std::cos(active_minigame.golf.aim_radians),
                                0.0f,
                                std::sin(active_minigame.golf.aim_radians));
        const float aim_len = 0.35f + active_minigame.golf.power * 0.65f;
        append_mesh(scene.debug_world,
                    build_debug_line_mesh(
                        board_point(ball + glm::vec3(0.0f, 0.08f, 0.0f)),
                        board_point(ball + glm::vec3(0.0f, 0.08f, 0.0f) +
                                    aim_dir * aim_len),
                        0.03f, glm::vec3(1.0f, 0.9f, 0.3f)));

        const glm::vec3 power_anchor = glm::vec3(-0.90f, 0.02f, -0.68f);
        append_board_voxel(power_anchor, glm::vec3(0.12f, 0.02f, 0.02f),
                           glm::vec3(0.2f, 0.2f, 0.24f));
        append_board_voxel(
            power_anchor +
                glm::vec3((-0.12f + active_minigame.golf.power * 0.24f), 0.03f,
                          0.0f),
            glm::vec3(std::max(0.02f, active_minigame.golf.power * 0.12f),
                      0.015f, 0.015f),
            glm::vec3(0.2f + active_minigame.golf.power * 0.8f, 0.7f, 0.25f));
      } else if (active_minigame.type == MiniGameType::Tetris) {
        const float cell_size = 0.18f;
        const float row_height = 0.095f;
        const glm::vec3 base(-0.90f, -0.46f, -0.68f);
        for (int y = 0; y < TetrisState::k_board_h; ++y) {
          for (int x = 0; x < TetrisState::k_board_w; ++x) {
            const uint8_t filled =
                active_minigame.tetris
                    .board[static_cast<size_t>(y * TetrisState::k_board_w + x)];
            if (filled == 0) {
              continue;
            }
            append_board_voxel(
                base + glm::vec3(x * cell_size, y * row_height, 0.0f),
                glm::vec3(0.075f, 0.040f, 0.045f),
                glm::vec3(0.78f, 0.42f, 0.92f));
          }
        }
        if (!active_minigame.completed) {
          for (int i = 0; i < 4; ++i) {
            const glm::ivec2 c =
                tetris_visual_cell(active_minigame.tetris.active_shape,
                                   active_minigame.tetris.active_rotation, i);
            const int x = active_minigame.tetris.active_x + c.x;
            const int y = active_minigame.tetris.active_y + c.y;
            if (x < 0 || x >= TetrisState::k_board_w || y < 0 ||
                y >= TetrisState::k_board_h) {
              continue;
            }
            append_board_voxel(
                base + glm::vec3(x * cell_size, y * row_height + 0.02f, 0.0f),
                glm::vec3(0.075f, 0.045f, 0.050f),
                glm::vec3(0.95f, 0.75f, 0.25f));
          }
        }
      } else if (active_minigame.type == MiniGameType::Racing) {
        constexpr float track_len = 1.8f;
        constexpr float track_half_w = 0.16f;
        const glm::vec3 base(-0.86f, 0.0f, 0.0f);
        append_board_voxel(base + glm::vec3(track_len * 0.5f, 0.0f, 0.0f),
                           glm::vec3(track_len * 0.5f, 0.02f, track_half_w),
                           glm::vec3(0.22f, 0.22f, 0.24f));

        for (int cp = 0; cp < 4; ++cp) {
          const float t = static_cast<float>(cp) / 4.0f;
          const float x = t * track_len;
          append_board_voxel(base + glm::vec3(x, 0.05f, -track_half_w),
                             glm::vec3(0.02f, 0.06f, 0.02f),
                             glm::vec3(0.95f, 0.45f, 0.2f));
          append_board_voxel(base + glm::vec3(x, 0.05f, track_half_w),
                             glm::vec3(0.02f, 0.06f, 0.02f),
                             glm::vec3(0.95f, 0.45f, 0.2f));
        }

        const float progress = std::clamp(
            active_minigame.racing.track_progress / 65.0f, 0.0f, 1.0f);
        append_board_voxel(base + glm::vec3(progress * track_len, 0.05f, 0.0f),
                           glm::vec3(0.07f, 0.07f, 0.09f),
                           glm::vec3(1.0f, 0.75f, 0.2f));
      }
    }
  }

  if (!runtime_options.debug_collision_only) {
    if ((!render_skinned_avatar && !render_skeleton_only) ||
        collision_debug_enabled || runtime_options.devhud) {
      append_mesh(scene.debug_world, player_capsule);
      append_mesh(scene.debug_world, target_marker);
    }
    if (render_skinned_avatar) {
      const RenderMesh local_model = selected_player_model->build_render_mesh(
          local_player_animation, local_player.transform.position,
          local_player.transform.rotation,
          player_color_from_id(local_player.network_id));
      append_mesh(scene.debug_world, local_model);
    }
    if (render_skeleton_only ||
        (render_skinned_avatar &&
         (collision_debug_enabled || runtime_options.devhud))) {
      if (render_skinned_avatar) {
        selected_player_model->append_debug_skeleton(
            scene.debug_world, local_player_animation,
            local_player.transform.position, local_player.transform.rotation,
            glm::vec3(0.95f, 0.97f, 1.0f), 0.012f);
      } else {
        const SkeletonPose local_pose = SkeletalAnimator::sample_pose(
            local_player.anim_state, local_player.anim_phase,
            local_player.anim_blend);
        SkeletalAnimator::append_debug_skeleton(
            scene.debug_world, local_pose, local_player.transform.position,
            local_player.transform.rotation, glm::vec3(0.95f, 0.97f, 1.0f),
            0.012f);
      }
    }
  }

  if (runtime_options.splitscreen) {
    const AnimatedCapsuleShape p2_shape = animated_shape(
        static_cast<uint8_t>(local_player_secondary.anim_state),
        local_player_secondary.anim_phase, local_player_secondary.anim_blend,
        local_player_secondary.controller.capsuleRadius,
        local_player_secondary.controller.capsuleHeight,
        local_player_secondary.camera_rig.pivotHeight);
    RenderMesh p2_capsule = build_debug_capsule_mesh(
        local_player_secondary.transform.position, p2_shape.radius,
        p2_shape.height,
        player_color_from_id(local_player_secondary.network_id));
    RenderMesh p2_target = build_debug_sphere_mesh(
        local_player_secondary.transform.position +
            glm::vec3(0.0f, p2_shape.pivot_height, 0.0f),
        0.10f, glm::vec3(0.6f, 0.85f, 1.0f));
    if (!runtime_options.debug_collision_only) {
      if ((!render_skinned_avatar && !render_skeleton_only) ||
          collision_debug_enabled || runtime_options.devhud) {
        append_mesh(scene.debug_world, p2_capsule);
        append_mesh(scene.debug_world, p2_target);
      }
      if (render_skinned_avatar) {
        const RenderMesh p2_model = selected_player_model->build_render_mesh(
            local_player_secondary_animation,
            local_player_secondary.transform.position,
            local_player_secondary.transform.rotation,
            player_color_from_id(local_player_secondary.network_id));
        append_mesh(scene.debug_world, p2_model);
      }
      if (render_skeleton_only) {
        if (render_skinned_avatar) {
          selected_player_model->append_debug_skeleton(
              scene.debug_world, local_player_secondary_animation,
              local_player_secondary.transform.position,
              local_player_secondary.transform.rotation,
              glm::vec3(0.86f, 0.92f, 1.0f), 0.010f);
        } else {
          const SkeletonPose p2_pose =
              SkeletalAnimator::sample_pose(local_player_secondary.anim_state,
                                            local_player_secondary.anim_phase,
                                            local_player_secondary.anim_blend);
          SkeletalAnimator::append_debug_skeleton(
              scene.debug_world, p2_pose,
              local_player_secondary.transform.position,
              local_player_secondary.transform.rotation,
              glm::vec3(0.86f, 0.92f, 1.0f), 0.010f);
        }
      }
    }
  }

  if (collision_debug_enabled &&
      (runtime_options.devhud || runtime_options.debug_collision_only)) {
    for (const glm::ivec3 &cell : last_collision_debug.overlapped_voxels) {
      const glm::vec3 bmin(static_cast<float>(cell.x),
                           static_cast<float>(cell.y),
                           static_cast<float>(cell.z));
      const glm::vec3 bmax = bmin + glm::vec3(1.0f);
      RenderMesh overlap_box =
          build_debug_aabb_mesh(bmin, bmax, glm::vec3(0.95f, 0.15f, 0.15f));
      append_mesh(scene.debug_world, overlap_box);
    }

    RenderMesh ground_ray =
        build_debug_line_mesh(last_collision_debug.grounding_ray_origin,
                              last_collision_debug.grounding_ray_hit, 0.01f,
                              glm::vec3(1.0f, 1.0f, 0.2f));
    append_mesh(scene.debug_world, ground_ray);

    RenderMesh normal_line = build_debug_line_mesh(
        local_player.transform.position + glm::vec3(0.0f, 0.05f, 0.0f),
        local_player.transform.position + glm::vec3(0.0f, 0.05f, 0.0f) +
            last_collision_debug.contact_normal * 0.6f,
        0.01f, glm::vec3(1.0f, 0.4f, 0.1f));
    append_mesh(scene.debug_world, normal_line);
  }

  for (const auto &[player_id, render_player] : remote_render_players) {
    (void)player_id;
    if (runtime_options.debug_collision_only) {
      continue;
    }
    const float remote_y =
        std::isfinite(render_player.position.y)
            ? render_player.position.y
            : collision_world.find_spawn_height(
                  glm::vec2(render_player.position.x, render_player.position.z),
                  local_player.controller.capsuleRadius,
                  local_player.controller.capsuleHeight) +
                  0.05f;
    const AnimatedCapsuleShape remote_shape = animated_shape(
        render_player.anim_state, render_player.anim_phase,
        render_player.anim_blend, local_player.controller.capsuleRadius,
        local_player.controller.capsuleHeight,
        local_player.camera_rig.pivotHeight);
    const glm::vec3 remote_base =
        glm::vec3(render_player.position.x, remote_y, render_player.position.z);
    if (!render_skinned_avatar || collision_debug_enabled ||
        runtime_options.devhud) {
      RenderMesh remote_capsule = build_debug_capsule_mesh(
          remote_base, remote_shape.radius, remote_shape.height,
          player_color_from_id(player_id));
      append_mesh(scene.debug_world, remote_capsule);
    }
    if (render_skinned_avatar) {
      const RenderMesh remote_model = selected_player_model->build_render_mesh(
          render_player.animation_runtime, remote_base,
          render_player.orientation,
          player_color_from_id(player_id) * glm::vec3(1.08f, 1.08f, 1.08f));
      append_mesh(scene.debug_world, remote_model);
    }
  }
}
