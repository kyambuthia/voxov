#include "engine/engine.hpp"

#include "engine_core/memory.hpp"
#include "engine_core/timing.hpp"
#include "engine_presentation/debug_scene_builder.hpp"
#include "engine_render/debug_draw/debug_draw.hpp"
#include "engine_render/debug_text.hpp"
#include "engine_world/planet_debug.hpp"
#include "engine_world/planet_math.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cmath>
#include <string>

namespace {
using PerfClock = std::chrono::steady_clock;

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

uint64_t bytes_to_kib(uint64_t bytes) { return (bytes + 1023u) / 1024u; }

constexpr double k_earth_radius_meters = 3185500.0;
constexpr uint32_t k_planet_streamer_max_lod = 19;
constexpr uint32_t k_surface_generation_budget = 48;
constexpr uint32_t k_space_generation_budget = 12;
constexpr uint32_t k_surface_visible_chunk_limit = 320;
constexpr uint32_t k_space_visible_chunk_limit = 192;
constexpr float k_surface_lod_error_pixels = 2.0f;
constexpr float k_space_lod_error_pixels = 8.0f;

void disable_gameplay_actions(InputState &input) {
  input.move = glm::vec2(0.0f);
  input.jump_pressed = false;
  input.jump_held = false;
  input.interact_pressed = false;
  input.sprint_held = false;
  input.crouch_held = false;
}

glm::vec3 tangent_or_fallback(glm::vec3 value, glm::vec3 up,
                              glm::vec3 fallback) {
  value -= up * glm::dot(value, up);
  const float len = glm::length(value);
  if (len > 1.0e-5f) {
    return value / len;
  }

  fallback -= up * glm::dot(fallback, up);
  const float fallback_len = glm::length(fallback);
  if (fallback_len > 1.0e-5f) {
    return fallback / fallback_len;
  }

  return glm::vec3(1.0f, 0.0f, 0.0f);
}

glm::vec3 orbit_forward_from_planet_frame(float yaw_deg, float pitch_deg,
                                          glm::vec3 up) {
  const float yaw = glm::radians(yaw_deg);
  const float pitch = glm::radians(pitch_deg);
  const glm::vec3 north =
      tangent_or_fallback(glm::vec3(0.0f, 0.0f, 1.0f), up,
                          glm::vec3(1.0f, 0.0f, 0.0f));
  const glm::vec3 east = glm::normalize(glm::cross(north, up));
  const glm::vec3 tangent_forward =
      glm::normalize(north * std::cos(yaw) + east * std::sin(yaw));
  return glm::normalize(tangent_forward * std::cos(pitch) +
                        up * std::sin(pitch));
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
}

void enqueue_animation_runtime_events(EventBus &events,
                                      const PlayerEntity &player,
                                      const PlayerAnimationRuntime &runtime) {
  for (const PlayerAnimationFiredEvent &event : runtime.events()) {
    if (event.type == PlayerAnimEventType::Landing) {
      events.enqueue_fixed(PlayerLandedEvent{
          .player_id = player.network_id,
          .position = player.transform.position,
          .impact_speed = player.locomotion.landing_impact,
      });
    } else if (event.type == PlayerAnimEventType::SoundTrigger &&
               event.payload == "jump") {
      events.enqueue_fixed(PlayerJumpedEvent{
          .player_id = player.network_id,
          .position = player.transform.position,
      });
    }
  }
}

RenderMesh build_local_player_debug_mesh(
    const PlayerEntity &player,
    const PlayerAnimationRuntime &animation_runtime,
    bool devhud_enabled) {
  RuntimeDebugSceneSnapshot snapshot{};
  snapshot.devhud_enabled = devhud_enabled;
  snapshot.local_player = &player;
  snapshot.local_player_animation = &animation_runtime;
  return DebugSceneBuilder{}.build(snapshot);
}

bool planet_render_state_is_space(PlanetRenderState state) {
  return state == PlanetRenderState::Space ||
         state == PlanetRenderState::Ascending;
}

const char *planet_render_state_name(PlanetRenderState state) {
  switch (state) {
  case PlanetRenderState::Space:
    return "SPACE";
  case PlanetRenderState::Descending:
    return "DESCENDING";
  case PlanetRenderState::Surface:
    return "SURFACE";
  case PlanetRenderState::Ascending:
    return "ASCENDING";
  }
  return "UNKNOWN";
}
} // namespace

bool Engine::init(const EngineRuntimeOptions &options) {
  runtime_options = options;
  session_state_.gameplay_started = true;
  session_state_.menu_open = false;
  session_state_.selected_character = GuiMenu::Character::Capsule;

  platform_services = runtime_options.platform_services != nullptr
                          ? *runtime_options.platform_services
                          : PlatformServices::desktop_default();
  game_session.reset();
  event_bus_.clear();
  event_bus_.reserve(64, 1024, 2048);
  event_bus_.subscribe<CollisionEvent>(
      EventPhase::Fixed,
      [this](const CollisionEvent &, const EventContext &) {
        ++collision_count_;
      });
  event_bus_.subscribe<PlayerJumpedEvent>(
      EventPhase::Fixed,
      [](const PlayerJumpedEvent &event, const EventContext &context) {
        static uint64_t jumped_event_count = 0;
        ++jumped_event_count;
        spdlog::debug("PlayerJumpedEvent fixed tick={} player={} count={}",
                      context.tick, event.player_id, jumped_event_count);
      });
  event_bus_.subscribe<FrameStartedEvent>(
      EventPhase::Frame,
      [this](const FrameStartedEvent &, const EventContext &) {
        ++presentation_frame_events_seen_;
      });
  event_bus_.subscribe<HudMessageEvent>(
      EventPhase::Frame,
      [this](const HudMessageEvent &event, const EventContext &) {
        last_hud_message_ = "HUD message " + std::to_string(event.message_id) +
                            " (" +
                            std::to_string(event.duration_seconds) + "s)";
      });
  event_bus_.subscribe<NetworkClientConnectedEvent>(
      EventPhase::Frame,
      [this](const NetworkClientConnectedEvent &, const EventContext &) {
        ++net_events_seen_;
        last_net_status_ = "Connected";
      });
  event_bus_.subscribe<NetworkClientDisconnectedEvent>(
      EventPhase::Frame,
      [this](const NetworkClientDisconnectedEvent &, const EventContext &) {
        ++net_events_seen_;
        last_net_status_ = "Disconnected";
      });

  EnginePhysicsSettings settings{};
  settings.solver_backend = runtime_options.physics_backend;
  physics.init(settings);

  scene = RenderScene{};
  collision_world = VoxelCollisionWorld{nullptr};
  debug_planet_.radius = k_earth_radius_meters;
  debug_planet_.center = glm::dvec3(0.0, -debug_planet_.radius, 0.0);
  debug_planet_.voxel_size = 1.0;
  debug_planet_.chunks_per_face = 8;
  debug_planet_.seed = 0x56584f56504c4e54ull;
  planet_streamer_.init(debug_planet_, k_planet_streamer_max_lod);
  planet_streamer_.set_config(PlanetStreamerConfig{
      .generation_budget_per_update = k_surface_generation_budget,
      .max_visible_chunks = k_surface_visible_chunk_limit,
      .lod_error_threshold_pixels = k_surface_lod_error_pixels,
  });
  collision_world = VoxelCollisionWorld{nullptr};
  collision_world.set_planet_surface_collider(
      glm::vec3(debug_planet_.center),
      static_cast<float>(debug_planet_.radius),
      static_cast<float>(planet_streamer_.max_height_above_base()),
      [this](glm::vec3 direction) {
        return static_cast<float>(
            planet_streamer_.height_above_base_at_direction(
                glm::dvec3(direction)));
      });
  atmosphere_transition_.configure(AtmosphereTransitionConfig{
      .surface_altitude = 750.0,
      .space_altitude = 1500.0,
      .fade_seconds = 0.5,
  });
  atmosphere_transition_.reset_to_surface(
      debug_planet_, glm::dvec3(0.0, 8.0, 0.0));
  active_planet_render_state_ = PlanetRenderState::Surface;

  local_player = PlayerControllerSystem::spawn_player(collision_world);
  local_player.controller.capsuleRadius = 0.7f;
  local_player.transform.position =
      glm::vec3(voxel_world_pos(debug_planet_, PlanetFace::PosY, 0.0, 0.0,
                                planet_streamer_.height_above_base_at_direction(
                                    glm::dvec3(0.0, 1.0, 0.0)) +
                                    2.0));
  local_player.camera_rig.pitch = -32.0f;
  local_player.camera_rig.distance = 7.5f;
  local_player.camera_rig.maxDistance = 24.0f;
  local_player_prev_position = local_player.transform.position;
  local_player_animation.reset(local_player.anim_state);
  camera.z_far = 20000000.0f;
  update_third_person_camera(local_player, camera);
  debug_planet_camera_face_ = debug_planet_camera_face(
      debug_planet_, glm::dvec3(camera.transform.position),
      glm::dvec3(camera.forward()));
  scene.debug_world = build_local_player_debug_mesh(
      local_player, local_player_animation, session_state_.devhud_enabled);
  if (session_state_.devhud_enabled) {
    append_mesh(scene.debug_world,
                build_debug_planet_face_highlight_mesh(
                    debug_planet_, debug_planet_camera_face_, 0.055f));
  }
  update_planet_scene_meshes(active_planet_render_state_,
                             RenderSurface{1280, 720, 1.0f});
  refresh_overlay_text();

  spdlog::info("Engine init: streamed planet terrain, capsule player, backend={}",
               runtime_options.render_backend == RenderBackendType::Sokol
                   ? "Sokol"
                   : "OpenGL");

  if (!renderer.init(RendererCreateInfo{
      .backend = runtime_options.render_backend,
  })) {
    std::fprintf(stderr, "Engine init failed: renderer init failed\n");
    physics.shutdown();
    return false;
  }
  renderer.upload_scene(scene);
  renderer.update_dynamic_meshes(scene.debug_world, scene.debug_screen);
  return true;
}

EngineConnectResult Engine::connect(const char *host, uint16_t port) {
  (void)host;
  (void)port;
  return EngineConnectResult::ConnectFailed;
}

void Engine::shutdown() {
  renderer.shutdown();
  physics.shutdown();
}

void Engine::tick(double frame_dt,
                  EngineInputFrame input_frame,
                  const RenderSurface &surface) {
  const PerfClock::time_point frame_cpu_start = PerfClock::now();
  const FixedStep &fixed = game_session.fixed_step();
  last_frame_dt = frame_dt;
  event_bus_.enqueue_frame(FrameStartedEvent{
      .frame = frame_index,
      .dt = static_cast<float>(frame_dt),
      .alpha = static_cast<float>(
          std::clamp(fixed.accumulator / fixed.fixed_dt, 0.0, 1.0)),
  });

  InputState gameplay_input = input_frame.primary;
  if (gameplay_input.debug_freeze_toggle_pressed) {
    debug_fly_mode_ = !debug_fly_mode_;
    local_player.locomotion.vertical_velocity = 0.0f;
    local_player.controller.velocity = glm::vec3(0.0f);
    last_hud_message_ = debug_fly_mode_ ? "Fly mode ON" : "Fly mode OFF";
  }
  if (session_state_.menu_open || !session_state_.gameplay_started) {
    disable_gameplay_actions(gameplay_input);
    gameplay_input.look_delta = glm::vec2(0.0f);
  }

  PlayerControllerSystem::update_camera_rig(
      local_player, gameplay_input, input_frame.touch_mode,
      static_cast<float>(frame_dt));

  ProfilingSnapshot profiling_sample{};
  bool jump_consumed = false;
  const RuntimeGameSessionCallbacks callbacks{
      .pump_server = []() {},
      .simulate_step =
          [this, &gameplay_input, &jump_consumed,
           &input_frame,
           &profiling_sample](const RuntimeGameSessionStepContext &step) {
            local_player_prev_position = local_player.transform.position;
            InputState step_input = gameplay_input;
            if (jump_consumed) {
              step_input.jump_pressed = false;
            }

            PlayerCollisionDebug collision_debug{};
            {
              ScopedCPUTimer timer(profiling_sample.gameplay_cpu_ms);
              collision_debug = PlayerControllerSystem::simulate_fixed(
                  local_player, step_input, collision_world, step.dt,
                  debug_fly_mode_);
            }
            {
              ScopedCPUTimer timer(profiling_sample.animation_cpu_ms);
              sync_local_animation_runtime(local_player, local_player_animation,
                                           step.dt);
              enqueue_animation_runtime_events(event_bus_, local_player,
                                               local_player_animation);
              local_player_animation.clear_events();
            }
            jump_consumed = jump_consumed || input_frame.primary.jump_pressed;
            {
              ScopedCPUTimer timer(profiling_sample.physics_cpu_ms);
              physics.step(step.dt);
            }
            if (collision_debug.had_collision) {
              event_bus_.enqueue_fixed(CollisionEvent{
                  .entity_a = local_player.network_id,
                  .entity_b = 0,
                  .point = local_player.transform.position,
                  .normal = collision_debug.contact_normal,
                  .impulse = collision_debug.penetration_correction,
              });
            }
            {
              ScopedCPUTimer timer(
                  profiling_sample.fixed_event_drain_cpu_ms);
              event_bus_.drain_fixed(EventContext{
                  .phase = EventPhase::Fixed,
                  .tick = step.tick,
                  .frame = frame_index,
                  .dt = step.dt,
              });
            }
          }};
  game_session.advance(frame_dt, callbacks);

  const RuntimeSessionSnapshot snapshot = session_snapshot();
  if (snapshot.connection_state != last_net_connection_state_) {
    if (snapshot.connection_state == NetClientConnectionState::Connected) {
      event_bus_.enqueue_frame(NetworkClientConnectedEvent{.client_id = 0});
    } else if (snapshot.connection_state ==
               NetClientConnectionState::Disconnected) {
      event_bus_.enqueue_frame(NetworkClientDisconnectedEvent{
          .client_id = 0,
          .reason = 0,
      });
    }
    last_net_connection_state_ = snapshot.connection_state;
  }

  input_frame.primary.jump_pressed = false;
  input_frame.primary.interact_pressed = false;

  const float alpha = static_cast<float>(
      std::clamp(fixed.accumulator / fixed.fixed_dt, 0.0, 1.0));
  {
    ScopedCPUTimer timer(profiling_sample.frame_event_drain_cpu_ms);
    event_bus_.drain_frame(EventContext{
        .phase = EventPhase::Frame,
        .tick = fixed.tick,
        .frame = frame_index,
        .dt = static_cast<float>(frame_dt),
        .alpha = alpha,
    });
  }
  update_third_person_camera(local_player, local_player.transform.position,
                             camera);
  scene.camera_origin.world_origin = glm::dvec3(camera.transform.position);
  atmosphere_transition_.update(
      debug_planet_, glm::dvec3(local_player.transform.position),
      glm::dvec3(local_player.controller.velocity), frame_dt);
  const AtmosphereTransitionSnapshot &planet_render =
      atmosphere_transition_.snapshot();
  active_planet_render_state_ = planet_render.state;
  update_planet_scene_meshes(active_planet_render_state_, surface);
  renderer.upload_scene(scene);
  debug_planet_camera_face_ = debug_planet_camera_face(
      debug_planet_, glm::dvec3(camera.transform.position),
      glm::dvec3(camera.forward()));

  render_stats.frame_ms =
      smooth_metric(render_stats.frame_ms, last_frame_dt * 1000.0, 0.20);
  if (last_frame_dt > 0.0) {
    render_stats.fps = smooth_metric(render_stats.fps, 1.0 / last_frame_dt,
                                     0.20);
  }
  render_stats.fixed_cpu_ms = smooth_metric(
      render_stats.fixed_cpu_ms, game_session.fixed_cpu_ms(), 0.25);
  render_stats.fixed_steps = game_session.fixed_steps_last_frame();
  render_stats.streamed_chunk_count =
      static_cast<uint32_t>(planet_streamer_.streamed_chunk_count());
  render_stats.profiling.gameplay_cpu_ms = smooth_metric(
      render_stats.profiling.gameplay_cpu_ms, profiling_sample.gameplay_cpu_ms,
      0.25);
  render_stats.profiling.animation_cpu_ms =
      smooth_metric(render_stats.profiling.animation_cpu_ms,
                    profiling_sample.animation_cpu_ms, 0.25);
  render_stats.profiling.physics_cpu_ms = smooth_metric(
      render_stats.profiling.physics_cpu_ms, profiling_sample.physics_cpu_ms,
      0.25);
  render_stats.profiling.fixed_event_drain_cpu_ms =
      smooth_metric(render_stats.profiling.fixed_event_drain_cpu_ms,
                    profiling_sample.fixed_event_drain_cpu_ms, 0.25);
  render_stats.profiling.frame_event_drain_cpu_ms =
      smooth_metric(render_stats.profiling.frame_event_drain_cpu_ms,
                    profiling_sample.frame_event_drain_cpu_ms, 0.25);
  const MemoryStats memory_stats = engine_memory_stats();
  render_stats.profiling.memory_current_allocations =
      memory_stats.current_allocations;
  render_stats.profiling.memory_total_allocations =
      memory_stats.total_allocations;
  render_stats.profiling.memory_current_bytes = memory_stats.current_bytes;
  render_stats.profiling.memory_total_bytes = memory_stats.total_bytes;

  scene.debug_world = build_local_player_debug_mesh(
      local_player, local_player_animation, session_state_.devhud_enabled);
  if (session_state_.devhud_enabled) {
    append_mesh(scene.debug_world,
                build_debug_planet_face_highlight_mesh(
                    debug_planet_, debug_planet_camera_face_, 0.055f));
  }
  refresh_overlay_text();
  renderer.update_dynamic_meshes(scene.debug_world, scene.debug_screen);

  RenderFrameContext ctx{};
  ctx.frame_index = frame_index++;
  ctx.alpha = fixed.accumulator / fixed.fixed_dt;
  ctx.delta_seconds = frame_dt;
  ctx.aspect_ratio = 16.0f / 9.0f;
  ctx.camera_origin = scene.camera_origin;
  ctx.view_count = 1u;
  ctx.views[0].camera = camera;
  ctx.views[0].viewport = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
  ctx.debug_xray = false;

  const PerfClock::time_point render_cpu_start = PerfClock::now();
  renderer.render_frame(ctx, render_stats, surface);

  const double render_cpu_ms = elapsed_ms(render_cpu_start, PerfClock::now());
  const double frame_cpu_ms = elapsed_ms(frame_cpu_start, PerfClock::now());
  render_stats.render_cpu_ms =
      smooth_metric(render_stats.render_cpu_ms, render_cpu_ms, 0.25);
  render_stats.cpu_ms = smooth_metric(render_stats.cpu_ms, frame_cpu_ms, 0.25);
}

const RenderStats &Engine::stats() const { return render_stats; }

void Engine::set_session_state(const EngineSessionState &state) {
  session_state_ = state;
  session_state_.selected_character = GuiMenu::Character::Capsule;
}

RuntimeSessionSnapshot Engine::session_snapshot() const {
  return RuntimeSessionSnapshot{};
}

void Engine::pump_lan_discovery() {}

bool Engine::pop_discovered_host(LanHostEntry &host) {
  host = LanHostEntry{};
  return false;
}

void Engine::abort_client_session() {}
void Engine::host_local_session() {}
void Engine::host_lan_session() {}
void Engine::join_nearby_session() {}
void Engine::leave_session() {}

void Engine::reset_camera() {
  local_player.camera_rig.yaw = 180.0f;
  local_player.camera_rig.pitch = -32.0f;
  local_player.camera_rig.distance = 7.5f;
}

GuiMenu::Character Engine::preferred_character() const {
  return GuiMenu::Character::Capsule;
}

void Engine::update_planet_scene_meshes(PlanetRenderState render_state,
                                        const RenderSurface &surface) {
  if (!planet_streamer_.initialized()) {
    scene.opaque_meshes.clear();
    return;
  }

  const bool space_render = planet_render_state_is_space(render_state);
  PlanetStreamerConfig config = planet_streamer_.config();
  config.generation_budget_per_update =
      space_render ? k_space_generation_budget : k_surface_generation_budget;
  config.max_visible_chunks =
      space_render ? k_space_visible_chunk_limit : k_surface_visible_chunk_limit;
  config.lod_error_threshold_pixels =
      space_render ? k_space_lod_error_pixels : k_surface_lod_error_pixels;
  planet_streamer_.set_config(config);

  const float aspect_ratio =
      static_cast<float>(std::max(1, surface.width)) /
      static_cast<float>(std::max(1, surface.height));
  const float screen_height_pixels =
      static_cast<float>(std::max(1, surface.height));
  const glm::mat4 view_projection =
      camera.projection(aspect_ratio) * camera.view();
  planet_streamer_.update(glm::dvec3(camera.transform.position),
                          view_projection, screen_height_pixels);
  scene.opaque_meshes = planet_streamer_.render_meshes();
}

void Engine::update_third_person_camera(PlayerEntity &player,
                                        Camera &out_camera) {
  update_third_person_camera(player, player.transform.position, out_camera);
}

void Engine::update_third_person_camera(PlayerEntity &player,
                                        const glm::vec3 &render_position,
                                        Camera &out_camera) {
  out_camera.clear_view_override();
  const glm::vec3 up = collision_world.has_planet_surface_collider()
                           ? collision_world.planet_up_at(render_position)
                           : glm::vec3(0.0f, 1.0f, 0.0f);
  const glm::vec3 pivot =
      render_position + up * player.camera_rig.pivotHeight;
  const glm::vec3 orbit_forward = collision_world.has_planet_surface_collider()
                                      ? orbit_forward_from_planet_frame(
                                            player.camera_rig.yaw,
                                            player.camera_rig.pitch, up)
                                      : PlayerControllerSystem::
                                            orbit_forward_from_angles(
                                                player.camera_rig.yaw,
                                                player.camera_rig.pitch);
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
  if (collision_world.has_planet_surface_collider()) {
    out_camera.set_view_override(glm::lookAt(camera_pos, pivot, up));
    return;
  }
  out_camera.transform.euler_radians.y = std::atan2(-view_dir.x, -view_dir.z);
  out_camera.transform.euler_radians.x =
      std::asin(std::clamp(view_dir.y, -1.0f, 1.0f));
  out_camera.transform.euler_radians.z = 0.0f;
}

void Engine::refresh_overlay_text() {
  scene.debug_screen = RenderMesh{};
  if (!session_state_.devhud_enabled) {
    return;
  }

  const ProfilingSnapshot &profiling = render_stats.profiling;
  std::string overlay_text =
      "FPS: " + std::to_string(static_cast<int>(render_stats.fps + 0.5)) +
      "\nCPU: " + std::to_string(render_stats.cpu_ms).substr(0, 5) +
      " ms  Frame: " + std::to_string(render_stats.frame_ms).substr(0, 5) +
      " ms" +
      "\nRender: " + std::to_string(render_stats.render_cpu_ms).substr(0, 5) +
      " ms  Fixed: " + std::to_string(render_stats.fixed_cpu_ms).substr(0, 5) +
      " ms x" + std::to_string(render_stats.fixed_steps) +
      "\nGameplay: " +
      std::to_string(profiling.gameplay_cpu_ms).substr(0, 5) +
      " ms  Anim: " +
      std::to_string(profiling.animation_cpu_ms).substr(0, 5) + " ms" +
      "\nPhysics: " + std::to_string(profiling.physics_cpu_ms).substr(0, 5) +
      " ms" +
      "\nEvents fixed/frame: " +
      std::to_string(profiling.fixed_event_drain_cpu_ms).substr(0, 5) +
      " / " +
      std::to_string(profiling.frame_event_drain_cpu_ms).substr(0, 5) +
      " ms" +
      "\nMemory allocs: " +
      std::to_string(profiling.memory_current_allocations) + "/" +
      std::to_string(profiling.memory_total_allocations) + "  KiB: " +
      std::to_string(bytes_to_kib(profiling.memory_current_bytes)) + "/" +
      std::to_string(bytes_to_kib(profiling.memory_total_bytes)) +
      "\nFrame events: " + std::to_string(presentation_frame_events_seen_) +
      "\nCollisions: " + std::to_string(collision_count_) +
      "\nNetwork events: " + std::to_string(net_events_seen_) +
      "\nNetwork status: " + last_net_status_ +
      "\nPlanet face: " +
      planet_face_debug_name(debug_planet_camera_face_);
  overlay_text += "\nFly mode: ";
  overlay_text += debug_fly_mode_ ? "ON (F4)" : "OFF (F4)";
  overlay_text += "\nPlanet render: ";
  overlay_text += planet_render_state_name(active_planet_render_state_);
  overlay_text += "\nPlanet chunks: ";
  overlay_text += std::to_string(render_stats.streamed_chunk_count);
  if (!last_hud_message_.empty()) {
    overlay_text += "\n" + last_hud_message_;
  }
  append_mesh(scene.debug_screen,
              build_screen_text_mesh(overlay_text, -0.92f, 0.90f, 0.0049f,
                                     glm::vec3(0.95f, 0.95f, 0.82f)));
}
