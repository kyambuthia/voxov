#include "engine/engine.hpp"

#include "engine_core/memory.hpp"
#include "engine_core/timing.hpp"
#include "engine_presentation/debug_scene_builder.hpp"
#include "engine_render/debug_draw/debug_draw.hpp"
#include "engine_render/debug_text.hpp"
#include "engine_world/wireframe_planet.hpp"
#include "engine_world/world_gen.hpp"
#include "engine_world/planet.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cmath>
#include <string>
#include <unordered_set>

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

void disable_gameplay_actions(InputState &input) {
  input.move = glm::vec2(0.0f);
  input.jump_pressed = false;
  input.jump_held = false;
  input.interact_pressed = false;
  input.sprint_held = false;
  input.crouch_held = false;
} // namespace

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
} // namespace

bool Engine::init(const EngineRuntimeOptions &options) {
  platform_services = options.platform_services != nullptr
                          ? *options.platform_services
                          : PlatformServices::desktop_default();
  runtime_options = options;
  runtime_options.platform_services = nullptr;
  session_state_.gameplay_started = true;
  session_state_.menu_open = false;
  session_state_.selected_character = GuiMenu::Character::Capsule;

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

  // ── Block-based voxel planet (Bowerbyte/Pec architecture) ───────────
  // 6 cube-face sectors → shells (doubling resolution/axis) → 16³ chunks.
  // 3D noise on sphere surface for seamless terrain.
  PlanetDefinition planet_def{};
  planet_def.center = glm::dvec3(0.0);
  planet_def.radius = 1000000.0;  // 1000 km (reduced from 2000 km for better float32 precision + playable scale)
  planet_def.voxel_size = 1.0;
  planet_def.chunks_per_face = 64;
  planet_def.seed = k_voxov_flat_world_seed;

  BlockWorldConfig bw_cfg{};
  bw_cfg.planet = planet_def;
  bw_cfg.surface_shells = 8;
  bw_cfg.base_resolution = 64;
  bw_cfg.block_size = 1.0;
  bw_cfg.chunk_size = 16;
  bw_cfg.seed = k_voxov_flat_world_seed;
  block_world_.init(bw_cfg);

  // Wireframe debug overlay.
  wireframe_planet_ = planet_def;
  wireframe_planet_.chunks_per_face = 64;
  wireframe_planet_dirty_ = true;

  // Planet-surface collision from block-world terrain.
  collision_world.set_planet_surface_collider(
      glm::vec3(planet_def.center),
      static_cast<float>(planet_def.radius),
      static_cast<float>(planet_terrain_max_height_above_base(planet_def)),
      [&bw = block_world_](glm::vec3 direction) -> float {
        return static_cast<float>(
            bw.terrain_height_at(glm::dvec3(direction)));
      });

  // Player spawn on planet surface using outer shell terrain height.
  local_player = PlayerControllerSystem::spawn_player(collision_world);
  local_player.controller.capsuleRadius = 0.7f;
  {
    const glm::dvec3 equator_dir = glm::normalize(glm::dvec3(1.0, 0.0, 0.0));
    const int32_t surf_voxels = block_world_.terrain_height_at(equator_dir);
    const ShellConfig &outer = block_world_.shell_config(
        block_world_.shell_count() - 1);
    // Map terrain voxel count to a radius within the thin outer shell.
    const double t = static_cast<double>(surf_voxels) /
                     static_cast<double>(outer.vertical_layers);
    const double surface_r = outer.inner_radius +
        t * (outer.outer_radius - outer.inner_radius);
    local_player.transform.position = glm::vec3(equator_dir * surface_r +
                                                 equator_dir * 3.0);
  }
  local_player.camera_rig.pitch = -16.0f;
  local_player.camera_rig.distance = 7.5f;
  local_player.camera_rig.maxDistance = 48.0f;
  local_player_prev_position = local_player.transform.position;
  local_player_animation.reset(local_player.anim_state);
  camera.z_far = 5000000.0f;
  camera.z_near = 0.5f;
  update_third_person_camera(local_player, camera);

  scene.debug_world = build_local_player_debug_mesh(
      local_player, local_player_animation, session_state_.devhud_enabled);
  refresh_overlay_text();

  spdlog::info("Engine init: block planet r={:.0f}km, shells={} fly=ON",
               planet_def.radius / 1000.0,
               block_world_.shell_count());

  // Initialize camera-relative snap origin at player spawn position.
  camera_snap_origin_ = glm::dvec3(local_player.transform.position);
  snap_origin_dirty_ = true;

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

bool Engine::capture_screenshot(const char *filepath, int width, int height) {
  return renderer.capture_screenshot(filepath, width, height);
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
  touch_controls_visible_ = input_frame.touch_mode;

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
  // ── Camera-relative rendering origin ─────────────────────────────────
  // Snap origin drifts when the camera moves >500 m from the current
  // origin.  When updated, all chunk meshes must be rebuilt so vertices
  // stay within float32 precision range (±500 m → sub-mm precision).
  {
    const glm::dvec3 cam_pos = glm::dvec3(camera.transform.position);
    const double drift = glm::distance(cam_pos, camera_snap_origin_);
    if (drift > 500.0) {
      camera_snap_origin_ = cam_pos;
      snap_origin_dirty_ = true;
    }
  }
  // Camera origin must be the snap origin so the GPU shader can compute
  // correct camera-relative positions for lighting.
  scene.camera_origin.world_origin = camera_snap_origin_;

  // ── Block world chunk streaming ─────────────────────────────────────
  // Load surface chunks in a radius around the player.  New chunks are
  // generated+meshed each frame (budget-limited).  When the player moves
  // to a new chunk center, stale meshes are evicted and the full desired
  // set is rebuilt.
  {
    const BlockAddress player_addr =
        block_world_.address_from_world(
            glm::dvec3(local_player.transform.position));
    const int32_t surface_shell = block_world_.shell_count() - 1;
    const int32_t chunk_radius = 4;  // larger local footprint for playable area of 1m voxels

    // Collect desired chunk addresses for the surface shell around the player.
    // We use a generous xz radius + the relevant radial y layers (player_cy and below)
    // so that the actual terrain surface blocks exist in memory.
    std::vector<BlockAddress> desired;
    const int32_t player_cy = player_addr.chunk.y;
    for (int32_t dy = -1; dy <= 0; ++dy) {
      const int32_t cy = player_cy + dy;
      if (cy < 0) continue;
      for (int32_t cz = -chunk_radius; cz <= chunk_radius; ++cz) {
        for (int32_t cx = -chunk_radius; cx <= chunk_radius; ++cx) {
          BlockAddress addr{};
          addr.sector = player_addr.sector;
          addr.shell  = surface_shell;
          addr.chunk  = glm::ivec3(
              player_addr.chunk.x + cx,
              cy,
              player_addr.chunk.z + cz);
          desired.push_back(addr);
        }
      }
    }

    // Generate missing chunks (budgeted). Higher budget here to fill the local
    // playable surface patch quickly for debugging/visibility.
    const uint32_t gen_budget = 32;
    std::vector<BlockAddress> new_chunks;
    for (const BlockAddress &addr : desired) {
      if (new_chunks.size() >= gen_budget) break;
      if (block_world_.find_chunk(addr) != nullptr) continue;
      block_world_.get_or_generate_chunk(addr);
      new_chunks.push_back(addr);
    }

    // FORCE population of scene.opaque_meshes from current desired every frame.
    // WHY: previous smart "only on moved/new/dirty + incremental append + prune"
    // logic + initial hash + snap_dirty interactions could leave opaque_meshes
    // empty or stale even when chunks existed with surface data. For getting a
    // visible voxel surface first, we clear and rebuild the list for exactly the
    // player-local desired chunks every tick. This is wasteful (we'll optimize
    // back to incremental + dirty tracking once the surface is confirmed visible
    // and playable). Combined with the renderer bypass of world-frustum culling
    // on the local resident set, this guarantees that if build_chunk_mesh
    // produces non-empty meshes, they will be submitted and drawn via rel_vp.
    scene.opaque_meshes.clear();
    for (const BlockAddress &addr : desired) {
      BlockAddress ck = addr;
      ck.block = glm::ivec3(0);
      const VoxelChunk *chunk = block_world_.find_chunk(ck);
      if (chunk == nullptr) continue;

      auto solid_at = [this, &ck, chunk](const BlockAddress &na) -> bool {
        BlockAddress nk = na;
        nk.block = glm::ivec3(0);
        const VoxelChunk *nc = (nk == ck) ? chunk : block_world_.find_chunk(nk);
        if (nc == nullptr) return false;
        return nc->solid(na.block.x, na.block.y, na.block.z);
      };

      RenderMesh mesh = block_world_.build_chunk_mesh(ck, *chunk, solid_at,
                                                      camera_snap_origin_);
      if (!mesh.vertices.empty()) {
        spdlog::info("VOXEL: built+submitted mesh sector={} shell={} cy={} cxz=({},{}) verts={}",
                     (int)ck.sector, ck.shell, ck.chunk.y, ck.chunk.x, ck.chunk.z,
                     mesh.vertices.size());
        scene.opaque_meshes.push_back(std::move(mesh));
      }
    }
    spdlog::info("VOXEL: opaque_meshes now has {} entries (desired around player cy={})",
                 scene.opaque_meshes.size(), player_cy);
  }

  // Wireframe overlay — TEMPORARILY suppressed during voxel surface debugging so the
  // 1 m block meshes (if any are being submitted) are not hidden under the coarse
  // grid. Restore the push once we confirm voxels appear.
  scene.wireframe_meshes.clear();
  // (wireframe push commented out for diagnosis)
  // scene.wireframe_meshes.push_back(wireframe_planet_mesh_);

  renderer.upload_scene(scene);

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
      static_cast<uint32_t>(block_world_.chunk_count());
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
  refresh_overlay_text();
  renderer.update_dynamic_meshes(scene.debug_world, scene.debug_screen);

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

void Engine::update_third_person_camera(PlayerEntity &player,
                                        Camera &out_camera) {
  update_third_person_camera(player, player.transform.position, out_camera);
}

void Engine::update_third_person_camera(PlayerEntity &player,
                                        const glm::vec3 &render_position,
                                        Camera &out_camera) {
  out_camera.clear_view_override();
  const glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
  const glm::vec3 pivot =
      render_position + up * player.camera_rig.pivotHeight;
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
  scene.debug_screen = RenderMesh{};

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
      append_screen_label(scene.debug_screen,
                          "Touch: RUN=DOWN  CROUCH=UP  JUMP=SELECT  MENU=CLOSE",
                          -0.88f, -0.64f, 0.0048f,
                          glm::vec3(0.88f, 0.95f, 1.0f));
    }
    return;
  }

  if (touch_controls_visible_) {
    append_screen_label(scene.debug_screen, "MENU", -0.92f, 0.86f, 0.0047f);
    append_screen_label(scene.debug_screen, "GOD", 0.76f, 0.86f, 0.0047f,
                        glm::vec3(1.0f, 0.85f, 0.4f));
    append_screen_label(scene.debug_screen, "MOVE", -0.78f, -0.70f, 0.0062f);
    append_screen_label(scene.debug_screen, "LOOK / DRAG", 0.26f, -0.70f,
                        0.0054f);
    append_touch_button_hint(scene.debug_screen, 0.71f, -0.59f, 0.93f, -0.77f,
                             "JUMP");
    append_touch_button_hint(scene.debug_screen, 0.46f, -0.59f, 0.68f, -0.77f,
                             "RUN");
    append_touch_button_hint(scene.debug_screen, 0.71f, -0.36f, 0.93f, -0.54f,
                             "CROUCH");
  }

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
      "\nNetwork status: " + last_net_status_;
  overlay_text += "\nFly mode: ";
  overlay_text += debug_fly_mode_ ? "ON (F4)" : "OFF (F4)";
  overlay_text += "\nWorld chunks: ";
  overlay_text += std::to_string(render_stats.streamed_chunk_count);
  if (!last_hud_message_.empty()) {
    overlay_text += "\n" + last_hud_message_;
  }
  append_mesh(scene.debug_screen,
              build_screen_text_mesh(overlay_text, -0.92f, 0.90f, 0.0049f,
                                     glm::vec3(0.95f, 0.95f, 0.82f)));
}
