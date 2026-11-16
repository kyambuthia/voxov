#include "engine/engine.hpp"
#include "engine_core/memory.hpp"
#include "engine_core/timing.hpp"
#include "engine_gameplay/animation/skeletal_animator.hpp"
#include "engine_gameplay/player/block_hotbar.hpp"
#include "engine_gameplay/player/player_visuals.hpp"
#include "engine_gameplay/player/remote_avatar.hpp"
#include "engine_presentation/debug_scene_builder.hpp"
#include "engine_render/debug_draw/debug_draw.hpp"
#include "engine_render/debug_text.hpp"
#include "engine_world/world_gen.hpp"
#include "platform/platform_services.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <unordered_set>

namespace {
using PerfClock = std::chrono::steady_clock;

#if defined(VOXOV_PLATFORM_ANDROID) || defined(VOXOV_PLATFORM_WEB)
constexpr int32_t kFlatStreamRadiusChunks = 2;
constexpr int32_t kFlatEvictRadiusChunks = 4;
constexpr uint32_t kChunkGenerationBudget = 2;
constexpr uint32_t kChunkMeshBudget = 2;
#else
constexpr int32_t kFlatStreamRadiusChunks = 3;
constexpr int32_t kFlatEvictRadiusChunks = 5;
constexpr uint32_t kChunkGenerationBudget = 4;
constexpr uint32_t kChunkMeshBudget = 4;
#endif

constexpr double kSaveDebounceSeconds = 2.0;

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

void append_screen_rect(RenderMesh &dst, float x0, float y0, float x1,
                        float y1, const glm::vec3 &color) {
  RenderMesh rect{};
  rect.vertices.push_back({glm::vec3(x0, y0, 0.0f), color});
  rect.vertices.push_back({glm::vec3(x1, y0, 0.0f), color});
  rect.vertices.push_back({glm::vec3(x1, y1, 0.0f), color});
  rect.vertices.push_back({glm::vec3(x0, y1, 0.0f), color});
  rect.indices.insert(rect.indices.end(), {0, 1, 2, 0, 2, 3});
  append_mesh(dst, rect);
}

void append_screen_label(RenderMesh &dst, const std::string &text, float x,
                         float y, float scale,
                         const glm::vec3 &color = glm::vec3(0.92f, 0.96f,
                                                            1.0f)) {
  append_mesh(dst, build_screen_text_mesh(text, x, y, scale, color));
}

void append_touch_button_hint(RenderMesh &dst, float x0, float y0, float x1,
                              float y1, const std::string &label) {
  append_screen_rect(dst, x0, y0, x1, y1, glm::vec3(0.05f, 0.07f, 0.09f));
  append_screen_label(dst, label, x0 + 0.025f, y0 - 0.045f, 0.0048f);
}

void append_block_hotbar(RenderMesh &dst, size_t selected, float aspect) {
  constexpr float kSlot = 0.062f;
  constexpr float kGap = 0.014f;
  constexpr float kRight = 0.94f;
  constexpr float kTop = -0.66f;
  const float slot_height = kSlot * std::max(aspect, 0.1f);
  const float left = kRight - static_cast<float>(kBlockHotbarSize) * kSlot -
                     static_cast<float>(kBlockHotbarSize - 1) * kGap;
  append_screen_rect(dst, left - 0.02f, kTop + 0.11f, kRight + 0.02f,
                     kTop - slot_height - 0.075f,
                     glm::vec3(0.04f, 0.07f, 0.10f));
  for (size_t i = 0; i < kBlockHotbarSize; ++i) {
    const float x0 = left + static_cast<float>(i) * (kSlot + kGap);
    const float x1 = x0 + kSlot;
    const bool active = i == selected;
    if (active) {
      append_screen_rect(dst, x0 - 0.008f, kTop + 0.008f * aspect,
                         x1 + 0.008f, kTop - slot_height - 0.008f * aspect,
                         glm::vec3(1.0f, 0.86f, 0.32f));
    }
    append_screen_rect(dst, x0, kTop, x1, kTop - slot_height,
                       kBlockHotbar[i].swatch);
    append_screen_label(dst, std::to_string(i + 1), x0 + 0.004f,
                        kTop - slot_height - 0.022f, 0.0036f,
                        active ? glm::vec3(1.0f, 0.90f, 0.45f)
                               : glm::vec3(0.70f, 0.78f, 0.86f));
  }
  append_screen_label(dst, kBlockHotbar[selected].label, left,
                      kTop + 0.065f, 0.0042f, glm::vec3(0.95f, 0.97f, 1.0f));
}

void disable_gameplay_actions(InputState &input) {
  input.move = glm::vec2(0.0f);
  input.jump_pressed = false;
  input.jump_held = false;
  input.interact_pressed = false;
  input.sprint_held = false;
  input.crouch_held = false;
  input.left_click_pressed = false;
  input.right_click_pressed = false;
  input.hotbar_slot_pressed = -1;
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
    bool devhud_enabled,
    bool render_local_avatar,
    RuntimeDebugSceneSnapshot::AvatarStyle avatar_style) {
  RuntimeDebugSceneSnapshot snapshot{};
  snapshot.spherical_planet = false;
  snapshot.spherical_planet_center = glm::vec3(0.0f);
  snapshot.devhud_enabled = devhud_enabled;
  snapshot.render_local_avatar = render_local_avatar;
  snapshot.avatar_style = avatar_style;
  snapshot.local_player = &player;
  snapshot.local_player_animation = &animation_runtime;
  return DebugSceneBuilder{}.build(snapshot);
}

RuntimeDebugSceneSnapshot::AvatarStyle avatar_style_from_character(
    GuiMenu::Character character) {
  switch (character) {
  case GuiMenu::Character::Humanoid:
    return RuntimeDebugSceneSnapshot::AvatarStyle::Humanoid;
  case GuiMenu::Character::Skeleton:
    return RuntimeDebugSceneSnapshot::AvatarStyle::Skeleton;
  case GuiMenu::Character::Capsule:
    break;
  }
  return RuntimeDebugSceneSnapshot::AvatarStyle::Capsule;
}

BlockAddress persistent_address_from_flat(const FlatBlockAddress &addr) {
  VoxelChunkCoord2D coord{};
  int lx = 0;
  int lz = 0;
  FlatWorld::world_to_local(addr.wx, addr.wz, coord, lx, lz);
  return BlockAddress{
      .sector = PlanetFace::PosY,
      .shell = 0,
      .chunk = glm::ivec3(coord.x, 0, coord.z),
      .block = glm::ivec3(lx, addr.wy, lz),
  };
}

FlatBlockAddress flat_address_from_persistent(const BlockAddress &addr) {
  return FlatBlockAddress{
      .wx = addr.chunk.x * VoxelChunk::CHUNK_X + addr.block.x,
      .wy = addr.block.y,
      .wz = addr.chunk.z * VoxelChunk::CHUNK_Z + addr.block.z,
  };
}
} // namespace

bool Engine::init(const EngineRuntimeOptions &options) {
  platform_services = options.platform_services != nullptr
                          ? *options.platform_services
                          : PlatformServices::desktop_default();
  runtime_options = options;
  runtime_options.platform_services = nullptr;
  session_state_.gameplay_started = true;
  session_state_.menu_open = false;
  session_state_.selected_character = GuiMenu::Character::Humanoid;
  third_person_camera_initialized_ = false;
  third_person_distance_ = 4.5f;

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
  active_collision_chunks_.clear();
  dirty_chunk_meshes_.clear();
  chunk_mesh_revisions_.clear();
  next_chunk_revision_ = 1;

  FlatWorldConfig fw_cfg{};
  fw_cfg.seed = k_voxov_flat_world_seed;
  fw_cfg.stream_radius_chunks = kFlatStreamRadiusChunks;
  fw_cfg.evict_radius_chunks = kFlatEvictRadiusChunks;
  fw_cfg.voxel_scale = 1.0f;
  flat_world_.init(fw_cfg);

  chunk_generation_budget_ = kChunkGenerationBudget;
  mesh_build_budget_ = kChunkMeshBudget;

  // Generate initial spawn patch around (32, 32) so spawn_player finds solid ground.
  std::vector<VoxelChunkCoord2D> evicted;
  const glm::dvec3 initial_anchor(32.0, 20.0, 32.0);
  const auto initial_chunks =
      flat_world_.stream_around(initial_anchor, 64, evicted);
  for (const VoxelChunkCoord2D &c : initial_chunks) {
    dirty_chunk_meshes_.insert(c);
  }
  install_active_surface_collider();

  local_player = PlayerControllerSystem::spawn_player(collision_world);
  local_player.controller.capsuleRadius = 0.7f;
  local_player.camera_rig.pitch = -20.0f;
  local_player.camera_rig.distance = 4.5f;
  local_player.camera_rig.maxDistance = 12.0f;
  local_player.camera_rig.minDistance = 1.2f;
  local_player.camera_rig.pivotHeight = 1.35f;
  local_player.locomotion_tuning.flight_speed = 18.0f;
  local_player.locomotion_tuning.flight_sprint_base_speed = 48.0f;
  local_player.locomotion_tuning.flight_sprint_altitude_scale = 0.5f;
  local_player.locomotion_tuning.flight_sprint_max_speed = 120.0f;
  local_player.locomotion_tuning.flight_acceleration = 80.0f;
  local_player_prev_position = local_player.transform.position;
  local_player_animation.reset(local_player.anim_state);
  expedition_mission_.reset();

  camera.z_far = 512.0f;
  camera.z_near = 0.25f;
  update_first_person_camera(local_player, camera);

  debug_fly_mode_ = false;
  camera_snap_origin_ = glm::dvec3(local_player.transform.position);
  load_persistent_game();
  stream_flat_terrain();
  update_first_person_camera(local_player, camera);

  scene.debug_world = build_local_player_debug_mesh(
      local_player, local_player_animation, session_state_.devhud_enabled,
      /*render_local_avatar=*/true,
      avatar_style_from_character(session_state_.selected_character));
  refresh_overlay_text();

  spdlog::info("Engine init: endless flat world seed={:#x} spawn=({:.1f}, {:.1f}, {:.1f})",
               fw_cfg.seed, local_player.transform.position.x,
               local_player.transform.position.y,
               local_player.transform.position.z);

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
  if (!net_client_.is_initialized() && !net_client_.init()) {
    spdlog::error("NetClient init failed; cannot connect to {}:{}",
                  host ? host : "(null)", port);
    return EngineConnectResult::NetworkInitFailed;
  }
  if (!net_client_.connect(host, port)) {
    spdlog::error("NetClient connect failed to {}:{}",
                  host ? host : "(null)", port);
    return EngineConnectResult::ConnectFailed;
  }
  NetChunkInterest interest{};
  interest.body_id = 0;
  interest.radius = 2;
  net_client_.set_chunk_interest(interest);
  return EngineConnectResult::Connected;
}

void Engine::start_local_server(uint16_t port, bool loopback_only) {
  if (local_server_running_) {
    return;
  }
  if (!local_server_.init(port)) {
    spdlog::error("Local server failed to initialize on port {}", port);
    return;
  }
  local_server_loopback_ = loopback_only;
  local_server_running_ = true;
}

void Engine::stop_client_session() {
  net_client_.disconnect();
  local_player.network_id = 1;
}

void Engine::shutdown() {
  (void)save_persistent_game();
  lan_discovery_.stop();
  if (local_server_running_) {
    local_server_.shutdown();
    local_server_running_ = false;
  }
  net_client_.disconnect();
  net_client_.shutdown();
  renderer.shutdown();
  physics.shutdown();
}

bool Engine::capture_screenshot(const char *filepath, int width, int height) {
  return renderer.capture_screenshot(filepath, width, height);
}

void Engine::tick(double frame_dt, EngineInputFrame input_frame,
                  const RenderSurface &surface) {
  const PerfClock::time_point frame_cpu_start = PerfClock::now();
  const FixedStep &fixed = game_session.fixed_step();
  if (local_server_running_) {
    local_server_.pump();
  }
  net_client_.pump();
  if (net_client_.local_player_id() != 0) {
    local_player.network_id = net_client_.local_player_id();
  }
  last_frame_dt = frame_dt;
  event_bus_.enqueue_frame(FrameStartedEvent{
      .frame = frame_index,
      .dt = static_cast<float>(frame_dt),
      .alpha = static_cast<float>(
          std::clamp(fixed.accumulator / fixed.fixed_dt, 0.0, 1.0)),
  });

  InputState gameplay_input = input_frame.primary;
  apply_session_input(gameplay_input);
  touch_controls_visible_ = input_frame.touch_mode;

  PlayerControllerSystem::update_camera_rig(
      local_player, gameplay_input, input_frame.touch_mode,
      static_cast<float>(frame_dt));

  ProfilingSnapshot profiling_sample{};
  simulate_frame(gameplay_input, input_frame, frame_dt, profiling_sample);

  const double camera_altitude = update_camera(frame_dt, surface);

  update_block_interaction(gameplay_input);
  flush_pending_save(frame_dt);

  camera_snap_origin_ = glm::dvec3(camera.transform.position);
  scene.camera_origin.world_origin = camera_snap_origin_;

  const TerrainStreamTimings terrain_timings = stream_flat_terrain();
  append_remote_player_meshes();

  const PerfClock::time_point gpu_upload_start = PerfClock::now();
  renderer.upload_scene(scene);
  const double gpu_upload_ms = elapsed_ms(gpu_upload_start, PerfClock::now());

  update_render_stats(profiling_sample, terrain_timings, gpu_upload_ms);

  scene.debug_world = build_local_player_debug_mesh(
      local_player, local_player_animation, session_state_.devhud_enabled,
      local_player.camera_rig.distance > 0.1f,
      avatar_style_from_character(session_state_.selected_character));
  append_target_outline(scene.debug_world);
  refresh_overlay_text();
  renderer.update_dynamic_meshes(scene.debug_world, scene.debug_screen);

  (void)camera_altitude;
  const RenderFrameContext ctx = build_render_context(frame_dt, surface);

  const PerfClock::time_point render_cpu_start = PerfClock::now();
  renderer.render_frame(ctx, render_stats, surface);

  const double render_cpu_ms = elapsed_ms(render_cpu_start, PerfClock::now());
  const double frame_cpu_ms = elapsed_ms(frame_cpu_start, PerfClock::now());
  render_stats.render_cpu_ms =
      smooth_metric(render_stats.render_cpu_ms, render_cpu_ms, 0.25);
  render_stats.cpu_ms = smooth_metric(render_stats.cpu_ms, frame_cpu_ms, 0.25);
}

void Engine::apply_session_input(InputState &gameplay_input) {
  if (gameplay_input.camera_toggle_pressed &&
      session_state_.gameplay_started && !session_state_.menu_open) {
    CameraRig &rig = local_player.camera_rig;
    third_person_camera_initialized_ = false;
    if (rig.distance > 0.0f) {
      third_person_distance_ = rig.distance;
      rig.distance = 0.0f;
      last_hud_message_ = "First-person view";
    } else {
      rig.distance = third_person_distance_;
      last_hud_message_ = "Third-person view — mouse wheel zooms";
    }
  }

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
  if (gameplay_input.hotbar_slot_pressed >= 0 &&
      static_cast<size_t>(gameplay_input.hotbar_slot_pressed) <
          kBlockHotbarSize) {
    hotbar_slot_ = static_cast<size_t>(gameplay_input.hotbar_slot_pressed);
    last_hud_message_ =
        std::string("Placing ") + kBlockHotbar[hotbar_slot_].label;
  }
}

double Engine::update_camera(double frame_dt, const RenderSurface &surface) {
  update_first_person_camera(local_player, local_player.transform.position,
                             camera, static_cast<float>(frame_dt));

  const float camera_ease = 1.0f - std::exp(
      -6.0f * static_cast<float>(std::clamp(frame_dt, 0.0, 0.1)));
  camera.z_near = 0.25f;
  camera.z_far = 512.0f;
  viewport_aspect_ratio_ = static_cast<float>(surface.width) /
                           static_cast<float>(std::max(1, surface.height));
  const float flight_speed = glm::length(local_player.controller.velocity);
  const float speed_fov =
      debug_fly_mode_ ? std::clamp(flight_speed / 120.0f, 0.0f, 1.0f) * 12.0f
                      : 0.0f;
  camera.fov_y_radians = glm::mix(
      camera.fov_y_radians, glm::radians(70.0f + speed_fov), camera_ease);
  return std::max(0.0, static_cast<double>(camera.transform.position.y));
}

void Engine::simulate_frame(InputState &gameplay_input,
                            EngineInputFrame &input_frame, double frame_dt,
                            ProfilingSnapshot &profiling_sample) {
  const FixedStep &fixed = game_session.fixed_step();
  bool jump_consumed = false;
  const RuntimeGameSessionCallbacks callbacks{
      .pump_server =
          [this]() {
            if (local_server_running_) {
              local_server_.pump();
            }
            net_client_.pump();
          },
      .simulate_step =
          [this, &gameplay_input, &jump_consumed, &input_frame,
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
            sync_network_state(static_cast<uint32_t>(step.tick), step_input);
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
}

Engine::TerrainStreamTimings Engine::stream_flat_terrain() {
  const PerfClock::time_point gen_start = PerfClock::now();
  const glm::dvec3 player_pos(local_player.transform.position);
  const VoxelChunkCoord2D player_chunk =
      FlatWorld::world_pos_to_chunk(player_pos, flat_world_.config().voxel_scale);
  if (player_chunk.x != last_collision_center_.x ||
      player_chunk.z != last_collision_center_.z) {
    collision_chunks_dirty_ = true;
  }

  std::vector<VoxelChunkCoord2D> evicted;
  const std::vector<VoxelChunkCoord2D> generated =
      flat_world_.stream_around(player_pos, chunk_generation_budget_, evicted);

  for (const VoxelChunkCoord2D &c : generated) {
    apply_persistent_edits_to_chunk(c);
    dirty_chunk_meshes_.insert(c);
    dirty_chunk_meshes_.insert(VoxelChunkCoord2D{c.x - 1, c.z});
    dirty_chunk_meshes_.insert(VoxelChunkCoord2D{c.x + 1, c.z});
    dirty_chunk_meshes_.insert(VoxelChunkCoord2D{c.x, c.z - 1});
    dirty_chunk_meshes_.insert(VoxelChunkCoord2D{c.x, c.z + 1});
    collision_chunks_dirty_ = true;
  }

  if (!evicted.empty()) {
    std::unordered_set<uint64_t> evicted_mesh_ids;
    for (const VoxelChunkCoord2D &c : evicted) {
      dirty_chunk_meshes_.erase(c);
      auto rev_it = chunk_mesh_revisions_.find(c);
      if (rev_it != chunk_mesh_revisions_.end()) {
        evicted_mesh_ids.insert(rev_it->second);
        chunk_mesh_revisions_.erase(rev_it);
      }
    }
    if (!evicted_mesh_ids.empty()) {
      scene.opaque_meshes.erase(
          std::remove_if(scene.opaque_meshes.begin(), scene.opaque_meshes.end(),
                         [&](const RenderMesh &m) {
                           return evicted_mesh_ids.find(m.mesh_id) !=
                                  evicted_mesh_ids.end();
                         }),
          scene.opaque_meshes.end());
    }
    collision_chunks_dirty_ = true;
  }

  if (collision_chunks_dirty_) {
    install_active_surface_collider();
  }
  const double chunk_gen_ms = elapsed_ms(gen_start, PerfClock::now());

  const PerfClock::time_point mesh_start = PerfClock::now();
  std::vector<VoxelChunkCoord2D> to_mesh;
  to_mesh.reserve(dirty_chunk_meshes_.size());
  for (const VoxelChunkCoord2D &c : dirty_chunk_meshes_) {
    if (flat_world_.find_chunk(c) != nullptr) {
      to_mesh.push_back(c);
    }
  }
  std::sort(to_mesh.begin(), to_mesh.end(),
            [&](const VoxelChunkCoord2D &a, const VoxelChunkCoord2D &b) {
              const int da = (a.x - player_chunk.x) * (a.x - player_chunk.x) +
                             (a.z - player_chunk.z) * (a.z - player_chunk.z);
              const int db = (b.x - player_chunk.x) * (b.x - player_chunk.x) +
                             (b.z - player_chunk.z) * (b.z - player_chunk.z);
              return da < db;
            });

  const float voxel_scale = flat_world_.config().voxel_scale;
  const uint32_t built_limit =
      std::min<uint32_t>(mesh_build_budget_, static_cast<uint32_t>(to_mesh.size()));
  for (uint32_t i = 0; i < built_limit; ++i) {
    const VoxelChunkCoord2D coord = to_mesh[i];
    dirty_chunk_meshes_.erase(coord);
    const VoxelChunk *chunk = flat_world_.find_chunk(coord);
    if (chunk == nullptr) {
      continue;
    }

    const int32_t world_base_x = coord.x * VoxelChunk::CHUNK_X;
    const int32_t world_base_z = coord.z * VoxelChunk::CHUNK_Z;
    RenderMesh mesh = chunk->build_greedy_mesh(
        glm::vec3(0.0f), voxel_scale, world_base_x, world_base_z,
        [this](int wx, int wy, int wz) {
          return flat_world_.solid_at(wx, wy, wz);
        });

    auto [id_it, inserted] =
        chunk_mesh_revisions_.emplace(coord, next_chunk_revision_);
    if (inserted) {
      ++next_chunk_revision_;
    }
    const uint64_t mesh_id = id_it->second;
    mesh.mesh_id = mesh_id;
    mesh.content_hash = next_chunk_revision_++;
    mesh.world_origin =
        glm::dvec3(static_cast<double>(world_base_x) * voxel_scale, 0.0,
                   static_cast<double>(world_base_z) * voxel_scale);

    auto existing = std::find_if(
        scene.opaque_meshes.begin(), scene.opaque_meshes.end(),
        [mesh_id](const RenderMesh &m) { return m.mesh_id == mesh_id; });
    if (existing != scene.opaque_meshes.end()) {
      *existing = std::move(mesh);
    } else {
      scene.opaque_meshes.push_back(std::move(mesh));
    }
  }
  const double mesh_build_ms = elapsed_ms(mesh_start, PerfClock::now());

  return TerrainStreamTimings{
      .chunk_gen_ms = chunk_gen_ms,
      .mesh_build_ms = mesh_build_ms,
  };
}

void Engine::append_remote_player_meshes() {
  for (const auto &[player_id, remote] : remote_avatars_) {
    (void)player_id;
    (void)remote;
  }
}

void Engine::update_render_stats(const ProfilingSnapshot &profiling_sample,
                                 const TerrainStreamTimings &terrain_timings,
                                 double gpu_upload_ms) {
  render_stats.frame_ms =
      smooth_metric(render_stats.frame_ms, last_frame_dt * 1000.0, 0.20);
  if (last_frame_dt > 0.0) {
    render_stats.fps = smooth_metric(render_stats.fps, 1.0 / last_frame_dt,
                                     0.20);
  }
  render_stats.fixed_cpu_ms = smooth_metric(
      render_stats.fixed_cpu_ms, game_session.fixed_cpu_ms(), 0.25);
  render_stats.fixed_steps = game_session.fixed_steps_last_frame();
  const NetDebugStats net_stats = net_client_.debug_stats();
  render_stats.net_connected = net_client_.is_connected();
  render_stats.net_local_player_id = net_client_.local_player_id();
  render_stats.net_remote_count =
      static_cast<uint32_t>(net_client_.player_states().size());
  render_stats.net_tx_packets_per_sec = net_stats.tx_packets_per_sec;
  render_stats.net_rx_packets_per_sec = net_stats.rx_packets_per_sec;
  render_stats.net_tx_bytes_per_sec = net_stats.tx_bytes_per_sec;
  render_stats.net_rx_bytes_per_sec = net_stats.rx_bytes_per_sec;
  render_stats.streamed_chunk_count =
      static_cast<uint32_t>(flat_world_.chunk_count());
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

  render_stats.chunk_gen_ms =
      smooth_metric(render_stats.chunk_gen_ms, terrain_timings.chunk_gen_ms);
  render_stats.mesh_build_ms =
      smooth_metric(render_stats.mesh_build_ms, terrain_timings.mesh_build_ms);
  render_stats.gpu_upload_ms =
      smooth_metric(render_stats.gpu_upload_ms, gpu_upload_ms);

  uint32_t opaque_verts = 0;
  uint32_t opaque_idxs = 0;
  for (const RenderMesh &mesh : scene.opaque_meshes) {
    opaque_verts += static_cast<uint32_t>(mesh.vertices.size());
    if (mesh.use_16_bit_indices) {
      opaque_idxs += static_cast<uint32_t>(mesh.indices16.size());
    } else {
      opaque_idxs += static_cast<uint32_t>(mesh.indices.size());
    }
  }
  render_stats.total_vertices = opaque_verts;
  render_stats.total_indices = opaque_idxs;
  render_stats.draw_call_count =
      static_cast<uint32_t>(scene.opaque_meshes.size());
}

RenderFrameContext Engine::build_render_context(double frame_dt,
                                                const RenderSurface &surface) {
  const FixedStep &fixed = game_session.fixed_step();
  RenderFrameContext ctx{};
  ctx.frame_index = frame_index++;
  ctx.alpha = fixed.accumulator / fixed.fixed_dt;
  ctx.delta_seconds = frame_dt;
  ctx.aspect_ratio = static_cast<float>(surface.width) /
                     static_cast<float>(std::max(1, surface.height));
  ctx.camera_origin = scene.camera_origin;
  ctx.view_count = 1u;
  ctx.views[0].camera = camera;
  ctx.views[0].viewport = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
  ctx.debug_xray = false;
  ctx.grayscale_view = false;
  ctx.blocky_sky = false;

  ctx.atmosphere.planet_center_radius =
      glm::vec4(0.0f, -6371000.0f, 0.0f, 6371000.0f);
  ctx.atmosphere.atm_params_1 = glm::vec4(80000.0f, 8500.0f, 1200.0f, 0.76f);
  ctx.atmosphere.rayleigh_scatter = glm::vec4(5.5e-6f, 13.0e-6f, 22.4e-6f, 0.0f);
  ctx.atmosphere.mie_scatter = glm::vec4(21.0e-6f, 0.0f, 0.0f, 0.0f);
  ctx.atmosphere.sun_dir_intensity = glm::vec4(sun_direction_, 20.0f);
  ctx.atmosphere.sky_color = glm::vec4(sky_color_, 1.0f);
  return ctx;
}

const RenderStats &Engine::stats() const { return render_stats; }

void Engine::set_session_state(const EngineSessionState &state) {
  const bool character_changed =
      session_state_.selected_character != state.selected_character;
  session_state_ = state;
  if (character_changed) {
    (void)save_persistent_game();
  }
}

RuntimeSessionSnapshot Engine::session_snapshot() const {
  RuntimeSessionSnapshot snapshot{};
  snapshot.connection_state = net_client_.connection_state();
  snapshot.hosting_local = local_server_running_ && local_server_loopback_;
  snapshot.hosting_lan = local_server_running_ && !local_server_loopback_;
  snapshot.has_session_info = net_client_.has_session_info();
  if (snapshot.has_session_info) {
    snapshot.session_info = net_client_.session_info();
  }
  snapshot.connect_target_host = net_client_.connect_target_host();
  snapshot.connect_target_port = net_client_.connect_target_port();
  return snapshot;
}

void Engine::pump_lan_discovery() { lan_discovery_.pump(); }

bool Engine::pop_discovered_host(LanHostEntry &host) {
  return lan_discovery_.pop_host(host);
}

void Engine::abort_client_session() { stop_client_session(); }

void Engine::host_local_session() {
#if defined(VOXOV_PLATFORM_WEB)
  last_net_status_ = "Browser clients join a native or dedicated server";
#else
  leave_session();
  start_local_server(7777, true);
  (void)connect("127.0.0.1", 7777);
#endif
}

void Engine::host_lan_session() {
#if defined(VOXOV_PLATFORM_WEB)
  last_net_status_ = "Browser hosting is unavailable; join through a gateway";
#else
  leave_session();
  start_local_server(7777, false);
  (void)connect("127.0.0.1", 7777);
  lan_discovery_.start_host(7777, "VOXOV Host");
#endif
}

void Engine::join_nearby_session() {
  stop_client_session();
  if (local_server_running_) {
    local_server_.shutdown();
    local_server_running_ = false;
    local_server_loopback_ = true;
  }
  lan_discovery_.stop();
#if defined(VOXOV_PLATFORM_WEB)
  last_net_status_ = "Use ?server=HOST&port=7777&proxy=ws://HOST:8080";
#else
  lan_discovery_.start_client();
#endif
}

void Engine::leave_session() {
  stop_client_session();
  lan_discovery_.stop();
  if (local_server_running_) {
    local_server_.shutdown();
    local_server_running_ = false;
  }
  local_server_loopback_ = true;
}

void Engine::sync_network_state(uint32_t sim_tick, const InputState &input) {
  if (!net_client_.is_connected()) {
    return;
  }
  NetTickInput tick_input{};
  tick_input.tick = sim_tick;
  tick_input.body_id = 0;
  tick_input.move_x = input.move.x;
  tick_input.move_y = input.move.y;
  tick_input.camera_yaw_deg = local_player.camera_rig.yaw;
  if (input.jump_held) {
    tick_input.action_flags |= net_flag(NetInputFlags::JumpHeld);
  }
  if (input.jump_pressed) {
    tick_input.action_flags |= net_flag(NetInputFlags::JumpPressed);
  }
  if (input.sprint_held) {
    tick_input.action_flags |= net_flag(NetInputFlags::SprintHeld);
  }
  if (input.crouch_held) {
    tick_input.action_flags |= net_flag(NetInputFlags::CrouchHeld);
  }
  net_client_.send_input(tick_input);
}

void Engine::reset_camera() {
  local_player.camera_rig.yaw = 0.0f;
  local_player.camera_rig.pitch = -20.0f;
  local_player.camera_rig.distance = 4.5f;
  third_person_camera_initialized_ = false;
  update_first_person_camera(local_player, camera);
}

GuiMenu::Character Engine::preferred_character() const {
  return session_state_.selected_character;
}

void Engine::install_active_surface_collider() {
  const glm::dvec3 pos(local_player.transform.position);
  last_collision_center_ =
      FlatWorld::world_pos_to_chunk(pos, flat_world_.config().voxel_scale);
  active_collision_chunks_ = flat_world_.collect_collision_chunks(pos, 1);
  collision_world = VoxelCollisionWorld(
      active_collision_chunks_, flat_world_.config().voxel_scale);
  collision_chunks_dirty_ = false;
}

void Engine::record_block_edit(const FlatBlockAddress &address,
                               VoxelMaterial material, bool solid) {
  const BlockAddress packed = persistent_address_from_flat(address);
  const auto existing = std::find_if(
      persistent_block_edits_.begin(), persistent_block_edits_.end(),
      [&](const PersistentBlockEdit &edit) {
        return edit.body_index == 0 && edit.address == packed;
      });
  const PersistentBlockEdit replacement{
      .body_index = 0,
      .address = packed,
      .material = material,
      .solid = solid,
  };
  if (existing != persistent_block_edits_.end()) {
    *existing = replacement;
  } else {
    persistent_block_edits_.push_back(replacement);
  }
}

void Engine::apply_persistent_edits_to_chunk(VoxelChunkCoord2D coord) {
  VoxelChunk *chunk = flat_world_.find_chunk(coord);
  if (chunk == nullptr) {
    return;
  }
  for (const PersistentBlockEdit &edit : persistent_block_edits_) {
    if (edit.address.chunk.x != coord.x || edit.address.chunk.z != coord.z) {
      continue;
    }
    const int lx = edit.address.block.x;
    const int ly = edit.address.block.y;
    const int lz = edit.address.block.z;
    if (lx < 0 || lx >= VoxelChunk::CHUNK_X || ly < 0 ||
        ly >= VoxelChunk::CHUNK_Y || lz < 0 || lz >= VoxelChunk::CHUNK_Z) {
      continue;
    }
    chunk->set_material(
        lx, ly, lz, edit.material,
        edit.solid ? VoxelChunk::kMaxBlockHeight : 0);
  }
}

void Engine::load_persistent_game() {
  persistent_block_edits_.clear();
  PersistentGameState state{};
  std::string error;
  if (!load_persistent_game_state(platform_services, state, error)) {
    if (std::filesystem::exists(platform_services.session_state_path())) {
      spdlog::warn("Could not load save state: {}", error);
    }
    return;
  }

  for (const PersistentBlockEdit &edit : state.block_edits) {
    if (edit.body_index != 0) {
      continue;
    }
    const FlatBlockAddress flat_addr = flat_address_from_persistent(edit.address);
    if (flat_addr.wy < 0 || flat_addr.wy >= VoxelChunk::CHUNK_Y) {
      continue;
    }
    persistent_block_edits_.push_back(edit);
    if (flat_world_.find_chunk(
            VoxelChunkCoord2D{edit.address.chunk.x, edit.address.chunk.z}) !=
        nullptr) {
      flat_world_.set_block(flat_addr.wx, flat_addr.wy, flat_addr.wz,
                            edit.solid ? edit.material : VoxelMaterial::Air);
    }
  }

  (void)expedition_mission_.restore(state.expedition_stage);
  session_state_.selected_character = state.character;
  if (glm::dot(state.player_local_position, state.player_local_position) > 1.0e-4) {
    local_player.transform.position = glm::vec3(state.player_local_position);
    third_person_camera_initialized_ = false;
    local_player_prev_position = local_player.transform.position;
    camera_snap_origin_ = state.player_local_position;
  }
  spdlog::info("Loaded flat world save: edits={}", persistent_block_edits_.size());
}

bool Engine::save_persistent_game() {
  PersistentGameState state{};
  state.expedition_stage = expedition_mission_.stage();
  state.character = session_state_.selected_character;
  state.active_body_index = 0;
  state.player_local_position = glm::dvec3(local_player.transform.position);
  state.block_edits = persistent_block_edits_;
  std::string error;
  if (!save_persistent_game_state(platform_services, state, error)) {
    spdlog::warn("Could not save game state: {}", error);
    return false;
  }
  return true;
}

void Engine::request_save() { persistence_dirty_ = true; }

void Engine::flush_pending_save(double frame_dt) {
  if (!persistence_dirty_) {
    return;
  }
  seconds_since_save_ += frame_dt;
  if (seconds_since_save_ < kSaveDebounceSeconds) {
    return;
  }
  if (save_persistent_game()) {
    persistence_dirty_ = false;
    seconds_since_save_ = 0.0;
  }
}

void Engine::invalidate_chunk_mesh(VoxelChunkCoord2D coord) {
  dirty_chunk_meshes_.insert(coord);
}

void Engine::invalidate_block_meshes(const FlatBlockAddress &addr) {
  VoxelChunkCoord2D coord{};
  int lx = 0;
  int lz = 0;
  FlatWorld::world_to_local(addr.wx, addr.wz, coord, lx, lz);
  invalidate_chunk_mesh(coord);
  if (lx == 0) {
    invalidate_chunk_mesh(VoxelChunkCoord2D{coord.x - 1, coord.z});
  } else if (lx == VoxelChunk::CHUNK_X - 1) {
    invalidate_chunk_mesh(VoxelChunkCoord2D{coord.x + 1, coord.z});
  }
  if (lz == 0) {
    invalidate_chunk_mesh(VoxelChunkCoord2D{coord.x, coord.z - 1});
  } else if (lz == VoxelChunk::CHUNK_Z - 1) {
    invalidate_chunk_mesh(VoxelChunkCoord2D{coord.x, coord.z + 1});
  }
}

void Engine::update_block_interaction(const InputState &input) {
  constexpr float k_reach = 7.0f;
  constexpr float k_eye_height = 1.6f;
  const glm::vec3 eye =
      local_player.transform.position + glm::vec3(0.0f, k_eye_height, 0.0f);
  const glm::vec3 ray_origin = camera.transform.position;
  const float max_dist = glm::length(eye - ray_origin) + k_reach;

  FlatBlockAddress hit{};
  glm::vec3 normal(0.0f);
  if (!flat_world_.raycast(glm::dvec3(ray_origin), glm::dvec3(camera.forward()),
                           max_dist, hit, normal)) {
    targeted_addr_.reset();
    return;
  }
  const glm::vec3 hit_center(static_cast<float>(hit.wx) + 0.5f,
                             static_cast<float>(hit.wy) + 0.5f,
                             static_cast<float>(hit.wz) + 0.5f);
  if (glm::length(hit_center - eye) > k_reach + 0.5f) {
    targeted_addr_.reset();
    return;
  }

  targeted_addr_ = hit;
  targeted_face_normal_ = normal;

  if (input.left_click_pressed) {
    flat_world_.set_block(hit.wx, hit.wy, hit.wz, VoxelMaterial::Air);
    expedition_mission_.on_block_removed(0);
    record_block_edit(hit, VoxelMaterial::Air, false);
    request_save();
    invalidate_block_meshes(hit);
    return;
  }

  if (input.right_click_pressed) {
    const FlatBlockAddress place{
        hit.wx + static_cast<int32_t>(std::lround(normal.x)),
        hit.wy + static_cast<int32_t>(std::lround(normal.y)),
        hit.wz + static_cast<int32_t>(std::lround(normal.z)),
    };
    if (place.wy < 0 || place.wy >= VoxelChunk::CHUNK_Y ||
        flat_world_.material_at(place.wx, place.wy, place.wz) !=
            VoxelMaterial::Air) {
      return;
    }
    const VoxelMaterial mat = kBlockHotbar[hotbar_slot_].material;
    flat_world_.set_block(place.wx, place.wy, place.wz, mat);
    expedition_mission_.on_block_placed(0);
    record_block_edit(place, mat, true);
    request_save();
    invalidate_block_meshes(place);
  }
}

void Engine::append_target_outline(RenderMesh &mesh) const {
  if (!targeted_addr_.has_value() || session_state_.menu_open) {
    return;
  }
  const FlatBlockAddress &a = *targeted_addr_;
  const float scale = flat_world_.config().voxel_scale;
  const glm::vec3 min_p =
      glm::vec3(static_cast<float>(a.wx), static_cast<float>(a.wy),
                static_cast<float>(a.wz)) *
      scale;
  const glm::vec3 max_p = min_p + glm::vec3(scale);
  const glm::vec3 center = (min_p + max_p) * 0.5f;
  constexpr float kInflate = 1.02f;
  const glm::vec3 c[8] = {
      center + (glm::vec3(min_p.x, min_p.y, min_p.z) - center) * kInflate,
      center + (glm::vec3(max_p.x, min_p.y, min_p.z) - center) * kInflate,
      center + (glm::vec3(min_p.x, max_p.y, min_p.z) - center) * kInflate,
      center + (glm::vec3(max_p.x, max_p.y, min_p.z) - center) * kInflate,
      center + (glm::vec3(min_p.x, min_p.y, max_p.z) - center) * kInflate,
      center + (glm::vec3(max_p.x, min_p.y, max_p.z) - center) * kInflate,
      center + (glm::vec3(min_p.x, max_p.y, max_p.z) - center) * kInflate,
      center + (glm::vec3(max_p.x, max_p.y, max_p.z) - center) * kInflate,
  };
  constexpr int kEdges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7},
                                 {0, 2}, {1, 3}, {4, 6}, {5, 7},
                                 {0, 4}, {1, 5}, {2, 6}, {3, 7}};
  const glm::vec3 color(0.06f, 0.07f, 0.08f);
  for (const auto &edge : kEdges) {
    append_mesh(mesh, build_debug_line_mesh(c[edge[0]], c[edge[1]], 0.018f, color));
  }
}

void Engine::update_first_person_camera(PlayerEntity &player, Camera &out_camera,
                                        float dt) {
  update_first_person_camera(player, player.transform.position, out_camera, dt);
}

void Engine::update_first_person_camera(PlayerEntity &player,
                                        const glm::vec3 &render_position,
                                        Camera &out_camera, float dt) {
  const glm::vec3 local_up(0.0f, 1.0f, 0.0f);
  const float yaw_rad = glm::radians(player.camera_rig.yaw);
  const float pitch_rad = glm::radians(player.camera_rig.pitch);
  const glm::vec3 forward(std::sin(yaw_rad) * std::cos(pitch_rad),
                          std::sin(pitch_rad),
                          std::cos(yaw_rad) * std::cos(pitch_rad));

  constexpr float k_eye_height = 1.6f;
  const bool third_person = player.camera_rig.distance > 0.0f;
  const float pivot_height =
      third_person ? player.camera_rig.pivotHeight : k_eye_height;
  glm::vec3 pivot = render_position + local_up * pivot_height;
  if (third_person) {
    constexpr float k_shoulder_offset = 0.65f;
    const glm::vec3 right = glm::cross(forward, local_up);
    if (glm::dot(right, right) > 1.0e-6f) {
      const glm::vec3 right_dir = glm::normalize(right);
      const float offset =
          k_shoulder_offset *
          std::clamp(player.camera_rig.distance / 2.5f, 0.0f, 1.0f);
      pivot += right_dir * offset;
    }
  }

  glm::vec3 orbit_origin = pivot;
  float orbit_distance = player.camera_rig.distance;
  if (third_person) {
    constexpr float k_position_alpha = 12.0f;
    constexpr float k_distance_alpha = 7.0f;
    if (!third_person_camera_initialized_) {
      third_person_pivot_ = pivot;
      third_person_smoothed_distance_ = orbit_distance;
      third_person_camera_initialized_ = true;
    } else {
      const float position_alpha =
          1.0f - std::exp(-k_position_alpha * std::max(dt, 0.0f));
      const float distance_alpha =
          1.0f - std::exp(-k_distance_alpha * std::max(dt, 0.0f));
      third_person_pivot_ += (pivot - third_person_pivot_) * position_alpha;
      third_person_smoothed_distance_ +=
          (player.camera_rig.distance - third_person_smoothed_distance_) *
          distance_alpha;
    }
    orbit_origin = third_person_pivot_;
    orbit_distance = third_person_smoothed_distance_;

    float hit_distance = 0.0f;
    if (collision_world.raycast(orbit_origin, -forward, orbit_distance + 0.25f,
                                hit_distance)) {
      orbit_distance = std::clamp(hit_distance - 0.25f, 0.2f, orbit_distance);
    }
  }

  const glm::vec3 eye = orbit_origin - forward * orbit_distance;
  out_camera.transform.position = eye;
  out_camera.set_view_override(glm::lookAt(eye, eye + forward, local_up));
  out_camera.transform.euler_radians.y = std::atan2(forward.x, -forward.z);
  out_camera.transform.euler_radians.x =
      -std::asin(std::clamp(forward.y, -1.0f, 1.0f));
  out_camera.transform.euler_radians.z = 0.0f;
}

void Engine::refresh_overlay_text() {
  scene.debug_screen = RenderMesh{};
  scene.debug_screen.content_hash = frame_index + 1;

  if (session_state_.menu_open) {
    append_screen_rect(scene.debug_screen, -0.95f, 0.90f, 0.32f, -0.86f,
                       glm::vec3(0.05f, 0.07f, 0.10f));
    append_screen_rect(scene.debug_screen, -0.93f, 0.87f, 0.30f, -0.83f,
                       glm::vec3(0.09f, 0.11f, 0.16f));

    const GuiMenuView &menu = session_state_.menu_view;
    float y = 0.80f;
    if (!menu.title.empty()) {
      append_screen_label(scene.debug_screen, menu.title, -0.88f, y, 0.0082f,
                          glm::vec3(0.98f, 0.98f, 1.0f));
      y -= 0.12f;
    }
    for (size_t i = 0; i < menu.items.size(); ++i) {
      const bool selected = static_cast<int>(i) == menu.selected;
      append_screen_label(scene.debug_screen,
                          selected ? "> " + menu.items[i]
                                   : "  " + menu.items[i],
                          -0.86f, y, 0.0067f,
                          selected ? glm::vec3(1.0f, 0.96f, 0.72f)
                                   : glm::vec3(0.86f, 0.91f, 0.98f));
      y -= 0.09f;
    }
    for (const std::string &line : menu.guide_lines) {
      append_screen_label(scene.debug_screen, line, -0.86f, y, 0.0056f,
                          glm::vec3(0.80f, 0.88f, 0.97f));
      y -= 0.07f;
    }
    if (!menu.status.empty()) {
      append_screen_label(scene.debug_screen, "STATUS: " + menu.status,
                          -0.88f, -0.76f, 0.0055f,
                          glm::vec3(0.88f, 0.93f, 0.99f));
    }
    if (touch_controls_visible_) {
      append_touch_button_hint(scene.debug_screen, -0.90f, -0.60f, -0.50f,
                               -0.70f, "MENU=CLOSE");
    }
    return;
  }

  append_block_hotbar(scene.debug_screen, hotbar_slot_, viewport_aspect_ratio_);

  if (!session_state_.devhud_enabled) {
    const glm::vec3 crosshair_color(0.95f, 0.95f, 0.95f);
    constexpr float ch_size = 0.018f;
    constexpr float ch_thick = 0.003f;
    append_screen_rect(scene.debug_screen, -ch_size, ch_thick, ch_size,
                       -ch_thick, crosshair_color);
    append_screen_rect(scene.debug_screen, -ch_thick, ch_size, ch_thick,
                       -ch_size, crosshair_color);
    return;
  }

  const ProfilingSnapshot &profiling = render_stats.profiling;
  const VoxelChunkCoord2D player_chunk = FlatWorld::world_pos_to_chunk(
      glm::dvec3(local_player.transform.position),
      flat_world_.config().voxel_scale);

  std::string overlay_text =
      "FPS: " + std::to_string(static_cast<int>(render_stats.fps + 0.5)) +
      "\nCPU: " + std::to_string(render_stats.cpu_ms).substr(0, 5) +
      " ms  Frame: " + std::to_string(render_stats.frame_ms).substr(0, 5) +
      " ms" +
      "\nRender: " + std::to_string(render_stats.render_cpu_ms).substr(0, 5) +
      " ms  Fixed: " + std::to_string(render_stats.fixed_cpu_ms).substr(0, 5) +
      " ms x" + std::to_string(render_stats.fixed_steps) +
      "\nChunk gen: " + std::to_string(render_stats.chunk_gen_ms).substr(0, 5) +
      " ms  Mesh build: " +
      std::to_string(render_stats.mesh_build_ms).substr(0, 5) + " ms" +
      "\nGPU upload: " +
      std::to_string(render_stats.gpu_upload_ms).substr(0, 5) +
      " ms  Draw calls: " + std::to_string(render_stats.draw_call_count) +
      "\nVertices: " + std::to_string(render_stats.total_vertices) +
      "  Indices: " + std::to_string(render_stats.total_indices) +
      "\nMemory allocs: " +
      std::to_string(profiling.memory_current_allocations) + "/" +
      std::to_string(profiling.memory_total_allocations) + "  KiB: " +
      std::to_string(bytes_to_kib(profiling.memory_current_bytes)) + "/" +
      std::to_string(bytes_to_kib(profiling.memory_total_bytes)) +
      "\nFly mode: " + std::string(debug_fly_mode_ ? "ON (F4)" : "OFF (F4)") +
      "\nFlat chunks: " + std::to_string(render_stats.streamed_chunk_count) +
      "  Opaques: " + std::to_string(scene.opaque_meshes.size()) +
      "\nPos: (" +
      std::to_string(static_cast<int>(local_player.transform.position.x)) +
      ", " +
      std::to_string(static_cast<int>(local_player.transform.position.y)) +
      ", " +
      std::to_string(static_cast<int>(local_player.transform.position.z)) +
      ")" +
      "\nChunk: (" + std::to_string(player_chunk.x) + ", " +
      std::to_string(player_chunk.z) + ")" +
      "\nTerrain h: " +
      std::to_string(static_cast<int>(flat_world_.surface_height_at(
          local_player.transform.position.x,
          local_player.transform.position.z))) +
      "\nGrounded: " +
      std::string(local_player.controller.grounded ? "YES" : "NO");

  if (targeted_addr_.has_value()) {
    const FlatBlockAddress &ta = *targeted_addr_;
    overlay_text += "\nTarget: (" + std::to_string(ta.wx) + "," +
                    std::to_string(ta.wy) + "," + std::to_string(ta.wz) + ")";
  } else {
    overlay_text += "\nTarget: NONE";
  }

  if (!last_hud_message_.empty()) {
    overlay_text += "\n" + last_hud_message_;
  }
  append_mesh(scene.debug_screen,
              build_screen_text_mesh(overlay_text, -0.92f, 0.90f, 0.0049f,
                                     glm::vec3(0.95f, 0.95f, 0.82f)));
}
