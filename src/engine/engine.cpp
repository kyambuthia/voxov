#include "engine/engine.hpp"

#include "engine_render/debug_draw/debug_draw.hpp"
#include "engine_render/debug_text.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
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

void disable_gameplay_actions(InputState &input) {
  input.move = glm::vec2(0.0f);
  input.jump_pressed = false;
  input.jump_held = false;
  input.interact_pressed = false;
  input.sprint_held = false;
  input.crouch_held = false;
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

  EnginePhysicsSettings settings{};
  settings.solver_backend = runtime_options.physics_backend;
  physics.init(settings);

  build_static_scene();
  local_player = PlayerControllerSystem::spawn_player(collision_world);
  local_player_prev_position = local_player.transform.position;
  local_player_animation.reset(local_player.anim_state);
  update_third_person_camera(local_player, camera);
  refresh_overlay_text();

  spdlog::info("Engine init: flat world, capsule player, backend={}",
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
  if (session_state_.menu_open || !session_state_.gameplay_started) {
    disable_gameplay_actions(gameplay_input);
    gameplay_input.look_delta = glm::vec2(0.0f);
  }

  PlayerControllerSystem::update_camera_rig(
      local_player, gameplay_input, input_frame.touch_mode,
      static_cast<float>(frame_dt));

  bool jump_consumed = false;
  const RuntimeGameSessionCallbacks callbacks{
      .pump_server = []() {},
      .simulate_step =
          [this, &gameplay_input, &jump_consumed,
           &input_frame](const RuntimeGameSessionStepContext &step) {
            local_player_prev_position = local_player.transform.position;
            InputState step_input = gameplay_input;
            if (jump_consumed) {
              step_input.jump_pressed = false;
            }

            const PlayerCollisionDebug collision_debug =
                PlayerControllerSystem::simulate_fixed(
                    local_player, step_input, collision_world, step.dt, false);
            sync_local_animation_runtime(local_player, local_player_animation,
                                         step.dt);
            enqueue_animation_runtime_events(event_bus_, local_player,
                                             local_player_animation);
            local_player_animation.clear_events();
            jump_consumed = jump_consumed || input_frame.primary.jump_pressed;
            physics.step(step.dt);
            if (collision_debug.had_collision) {
              event_bus_.enqueue_fixed(CollisionEvent{
                  .entity_a = local_player.network_id,
                  .entity_b = 0,
                  .point = local_player.transform.position,
                  .normal = collision_debug.contact_normal,
                  .impulse = collision_debug.penetration_correction,
              });
            }
            event_bus_.drain_fixed(EventContext{
                .phase = EventPhase::Fixed,
                .tick = step.tick,
                .frame = frame_index,
                .dt = step.dt,
            });
          }};
  game_session.advance(frame_dt, callbacks);

  input_frame.primary.jump_pressed = false;
  input_frame.primary.interact_pressed = false;

  const float alpha = static_cast<float>(
      std::clamp(fixed.accumulator / fixed.fixed_dt, 0.0, 1.0));
  event_bus_.drain_frame(EventContext{
      .phase = EventPhase::Frame,
      .tick = fixed.tick,
      .frame = frame_index,
      .dt = static_cast<float>(frame_dt),
      .alpha = alpha,
  });
  const glm::vec3 local_player_render_position = glm::mix(
      local_player_prev_position, local_player.transform.position, alpha);
  update_third_person_camera(local_player, local_player_render_position,
                             camera);

  refresh_overlay_text();
  renderer.update_dynamic_meshes(scene.debug_world, scene.debug_screen);

  render_stats.frame_ms =
      smooth_metric(render_stats.frame_ms, last_frame_dt * 1000.0, 0.20);
  render_stats.fixed_cpu_ms = smooth_metric(
      render_stats.fixed_cpu_ms, game_session.fixed_cpu_ms(), 0.25);
  render_stats.fixed_steps = game_session.fixed_steps_last_frame();
  render_stats.streamed_chunk_count =
      static_cast<uint32_t>(world_state.streamed_chunks.size());

  RenderFrameContext ctx{};
  ctx.frame_index = frame_index++;
  ctx.alpha = fixed.accumulator / fixed.fixed_dt;
  ctx.delta_seconds = frame_dt;
  ctx.aspect_ratio = 16.0f / 9.0f;
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
  local_player.camera_rig.pitch = -12.0f;
  local_player.camera_rig.distance = 5.0f;
}

GuiMenu::Character Engine::preferred_character() const {
  return GuiMenu::Character::Capsule;
}

void Engine::build_static_scene() {
  world_state.initialize(world_chunk, collision_world, scene);
  scene.debug_world = RenderMesh{};
  scene.debug_screen = RenderMesh{};
}

void Engine::update_third_person_camera(PlayerEntity &player,
                                        Camera &out_camera) {
  update_third_person_camera(player, player.transform.position, out_camera);
}

void Engine::update_third_person_camera(PlayerEntity &player,
                                        const glm::vec3 &render_position,
                                        Camera &out_camera) {
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

void Engine::refresh_overlay_text() {
  scene.debug_world = RenderMesh{};
  scene.debug_screen = RenderMesh{};
  std::string overlay_text =
      "Frame events: " + std::to_string(presentation_frame_events_seen_) +
      "\nCollisions: " + std::to_string(collision_count_);
  if (!last_hud_message_.empty()) {
    overlay_text += "\n" + last_hud_message_;
  }
  append_mesh(scene.debug_screen,
              build_screen_text_mesh(overlay_text, -0.92f, 0.90f, 0.0049f,
                                     glm::vec3(0.95f, 0.95f, 0.82f)));
}
