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
  planet_def.radius = 500.0;  // 500 m — small planet for debugging visibility
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

  // ── LOD system ──────────────────────────────────────────────────────────
  // WHY: distance-based chunk resolution reduces GPU vertex count for distant
  // chunks. screen-space error metric with hysteresis prevents popping.
  {
    LODConfig lod_cfg{};
    lod_cfg.error_threshold = 4.0f;      // pixels
    lod_cfg.hysteresis_factor = 1.5f;    // dead zone to prevent oscillation
    lod_cfg.max_lod_level = 3;           // LOD 3 = 2³ effective (coarsest)
    lod_system_.init(lod_cfg);
  }

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
    // Surface is at planet_radius + terrain_height_in_blocks * block_size.
    // Player spawns a few blocks above that.
    const double surface_r = block_world_.planet().radius +
        static_cast<double>(surf_voxels) * block_world_.config().block_size;
    local_player.transform.position = glm::vec3(equator_dir * (surface_r + 2.0));
  }
  local_player.camera_rig.pitch = -10.0f;   // slight downward look toward surface
  local_player.camera_rig.distance = 0.0f;   // first-person: no orbit distance
  local_player.camera_rig.maxDistance = 0.0f;
  local_player.camera_rig.minDistance = 0.0f;
  local_player.camera_rig.pivotHeight = 0.0f;
  local_player_prev_position = local_player.transform.position;
  local_player_animation.reset(local_player.anim_state);
  camera.z_far = 2000.0f;   // Scale z_far to planet size (500m radius → 2000m far)
  camera.z_near = 0.5f;     // z_far/z_near ratio = 4000:1, good float32 depth precision
  update_first_person_camera(local_player, camera);

  scene.debug_world = build_local_player_debug_mesh(
      local_player, local_player_animation, session_state_.devhud_enabled);
  refresh_overlay_text();

    // Start with fly mode OFF — gravity walks on the sphere surface.
    debug_fly_mode_ = false;

    // ── Solar system initialization ───────────────────────────────────
    // Keplerian orbits: Sun at origin, Planet (our voxel world) at ~2000m,
    // Moon at ~300m from planet.  The planet's orbital position determines
    // sun direction; the planet centre stays at origin for block-world.
    solar_system_.init();
    solar_system_time_ = 0.0;

    // ── Atmosphere initialization ────────────────────────────────────────
    // Rayleigh + Mie scattering for sky color and aerial perspective.
    // Scale heights are reduced proportionally to our 500m planet radius
    // (Earth scale heights: H_R=8000m, H_M=1200m for 6360km radius).
    // We use H_R=500m*8000/6360000=0.63m ≈ 1.0m, H_M=0.1m for visible effect.
    {
        AtmosphereParams atm_params{};
        atm_params.planet_radius = planet_def.radius;  // 500m
        atm_params.atmosphere_height = 40.0;            // 40m above surface
        // Scale Rayleigh scattering for small planet — boost coefficients
        // so the thin atmosphere has visible optical depth.
        atm_params.rayleigh_scattering = glm::dvec3(5.8e-4, 13.5e-4, 33.1e-4); // 100× Earth
        atm_params.mie_scattering = 21.0e-3;            // 100× Earth
        atm_params.rayleigh_scale_height = 5.0;          // H_R scaled to 500m planet
        atm_params.mie_scale_height = 2.0;               // H_M scaled
        atm_params.mie_asymmetry = 0.76;
        // Sun direction: initialise from solar system (planet at orbital pos at t=0).
        atm_params.sun_direction = solar_system_.sun_direction_from(glm::dvec3(0.0));
        atm_params.sun_intensity = 20.0;
        atm_params.view_ray_samples = 12;
        atm_params.light_ray_samples = 6;
        atmosphere_.init(atm_params);
        atmosphere_enabled_ = true;
    }

    spdlog::info("Engine init: block planet r={:.0f}m, shells={}, fly=OFF",
               planet_def.radius,
               block_world_.shell_count());
    // ── Flight vehicle spawn ──────────────────────────────────────────────
    // Spawn the vehicle on the planet surface near the player, with
    // initial forward direction pointing east (tangent to sphere).
    {
        VehicleConfig vcfg{};
        vcfg.mass = 1500.0;
        vcfg.wing_area = 25.0;
        vcfg.drag_coefficient = 0.025;
        vcfg.lift_coefficient = 0.7;
        vcfg.max_thrust = 60000.0;
        vcfg.fuel_capacity = 200.0;
        vcfg.fuel_consumption = 0.08;
        flight_vehicle_.init(vcfg);

        // Position: on the surface at the player's location offset by +3m radial.
        const glm::dvec3 surface_normal = glm::normalize(
            glm::dvec3(local_player.transform.position));
        const int32_t terrain_h = block_world_.terrain_height_at(surface_normal);
        const double surface_r = planet_def.radius +
            static_cast<double>(terrain_h) * bw_cfg.block_size;
        const glm::dvec3 vehicle_pos = surface_normal * (surface_r + 3.0);

        // Compute tangent directions for orientation at spawn.
        glm::dvec3 world_ref(0.0, 1.0, 0.0);
        if (std::abs(glm::dot(surface_normal, world_ref)) > 0.99)
            world_ref = glm::dvec3(0.0, 0.0, 1.0);
        const glm::dvec3 east = glm::normalize(glm::cross(world_ref, surface_normal));
        const glm::dvec3 north = glm::normalize(glm::cross(surface_normal, east));

        // Build quaternion from local tangent frame:
        // right = east, up = surface_normal, forward = north
        const glm::dmat3 rot(east, surface_normal, north);
        flight_vehicle_.state().position = vehicle_pos;
        flight_vehicle_.state().orientation = glm::quat_cast(rot);
        flight_vehicle_.state().forward = north;
        flight_vehicle_.state().up = surface_normal;
        flight_vehicle_.state().right = east;
        flight_vehicle_spawned_ = true;
    }

    spdlog::info("Player spawn: ({:.1f}, {:.1f}, {:.1f}), terrain_h={}, z_far={:.0f}",
               local_player.transform.position.x,
               local_player.transform.position.y,
               local_player.transform.position.z,
               block_world_.terrain_height_at(
                   glm::normalize(glm::dvec3(local_player.transform.position))),
               camera.z_far);

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

            // ── Flight vehicle physics ──────────────────────────────────
            // Update vehicle state each fixed step with aerodynamic forces.
            // WHY in simulate_step: physics should run at fixed rate for
            // deterministic integration, independent of render framerate.
            if (flight_vehicle_spawned_) {
              // Toggle engine with T key (consumed once per press).
              if (gameplay_input.engine_toggle_pressed) {
                flight_vehicle_.state().engine_active =
                    !flight_vehicle_.state().engine_active;
                last_hud_message_ = flight_vehicle_.state().engine_active
                    ? "Vehicle engine ON" : "Vehicle engine OFF";
              }

              // Throttle: Shift increases, Ctrl decreases.
              double throttle = flight_vehicle_.state().throttle;
              if (gameplay_input.sprint_held) {
                throttle = std::min(1.0, throttle + step.dt * 0.5);  // ramp up
              }
              if (gameplay_input.crouch_held) {
                throttle = std::max(0.0, throttle - step.dt * 0.5);  // ramp down
              }
              flight_vehicle_.set_throttle(throttle);

              // Flight controls: WASD for pitch/yaw, Q/E for roll.
              // W/S → pitch, A/D → yaw, Q/E → roll.
              constexpr double k_angular_rate = 2.0;  // rad/s at full input
              flight_vehicle_.apply_pitch(gameplay_input.move.y * k_angular_rate);
              flight_vehicle_.apply_yaw(gameplay_input.move.x * k_angular_rate);
              // Roll: Q (zoom_in) and E (interact key) for roll left/right.
              double roll_input = 0.0;
              if (gameplay_input.key_a) roll_input -= 0.5;  // A also rolls left
              if (gameplay_input.key_d) roll_input += 0.5;  // D also rolls right
              flight_vehicle_.apply_roll(roll_input * k_angular_rate);

              // Compute gravity toward planet centre.
              const glm::dvec3 vpos = flight_vehicle_.state().position;
              const double dist_from_center = glm::length(vpos);
              const glm::dvec3 grav_dir = (dist_from_center > 1e-6)
                  ? -glm::normalize(vpos) : glm::dvec3(0.0, -1.0, 0.0);
              // Gravity scales with inverse square of distance from planet centre.
              // Planet radius = 500m, surface gravity = 9.81 m/s².
              constexpr double k_planet_radius = 500.0;
              constexpr double k_surface_gravity = 9.81;
              const double grav_mag = k_surface_gravity *
                  (k_planet_radius / dist_from_center) *
                  (k_planet_radius / dist_from_center);
              const glm::dvec3 gravity = grav_dir * grav_mag;

              // Air density: exponential decay with altitude.
              // Scale height H ≈ 20m for our small 500m planet.
              const double altitude = dist_from_center - k_planet_radius;
              constexpr double k_sea_level_density = 1.225;  // kg/m³ at surface
              constexpr double k_scale_height = 20.0;         // m
              const double air_density = (altitude > 0.0)
                  ? k_sea_level_density * std::exp(-altitude / k_scale_height)
                  : k_sea_level_density;

              flight_vehicle_.update(step.dt, air_density, gravity);
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
  // First-person camera from player eye position.
  update_first_person_camera(local_player, local_player.transform.position,
                              camera);

  // ── Block interaction (raycast pick, break/place) ─────────────────────
  // Raycast from camera center forward to find targeted block.
  // Left click: break block (set to Air). Right click: place block.
  {
    constexpr float k_pick_range = 10.0f;
    const glm::vec3 ray_origin = camera.transform.position;
    const glm::vec3 ray_dir = camera.forward();

    // Step along ray in 0.5m increments (half block size), check block occupancy.
    // Convert each test point to BlockAddress and test if solid.
    glm::dvec3 hit_block_world = glm::dvec3(0.0);
    bool hit_found = false;
    float hit_dist = 0.0f;
    constexpr float k_step = 0.3f;  // sub-block step for reliable thin-wall detection
    for (float d = k_step; d <= k_pick_range; d += k_step) {
      const glm::vec3 test_pos = ray_origin + ray_dir * d;
      // Clamp to valid range: must be within ~planet radius + some buffer.
      const float r = glm::length(test_pos);
      if (r < 1.0f || r > 2000.0f) continue;
      const BlockAddress addr = block_world_.address_from_world(glm::dvec3(test_pos));
      const VoxelChunk *chunk = block_world_.find_chunk(addr);
      if (chunk == nullptr) continue;
      if (chunk->solid(addr.block.x, addr.block.y, addr.block.z) &&
          chunk->material(addr.block.x, addr.block.y, addr.block.z) != VoxelMaterial::Air) {
        hit_block_world = block_world_.world_from_address(addr);
        hit_dist = d;
        hit_found = true;
        break;
      }
    }

    // Store targeted block info for highlight and interaction.
    targeted_hit_pos_ = hit_block_world;
    targeted_addr_ = hit_found ? std::optional<BlockAddress>(block_world_.address_from_world(hit_block_world)) : std::nullopt;
    targeted_face_normal_ = glm::vec3(0.0f);

    if (hit_found && targeted_addr_.has_value()) {
      // Compute face normal by stepping back along the ray to find which
      // axis the ray crossed to enter the solid block. This is simpler and
      // more reliable than computing from the hit position relative to block center.
      const BlockAddress &addr = *targeted_addr_;
      glm::vec3 face_normal(0.0f);
      if (hit_dist > k_step) {
        const glm::vec3 prev_pos = ray_origin + ray_dir * (hit_dist - k_step);
        const BlockAddress prev_addr = block_world_.address_from_world(glm::dvec3(prev_pos));
        const glm::ivec3 diff = addr.block - prev_addr.block;
        // The axis with the largest absolute component is the face we entered through.
        // Use the sign to get the outward normal.
        if (addr.sector == prev_addr.sector && addr.shell == prev_addr.shell &&
            addr.chunk == prev_addr.chunk) {
          // Same chunk: diff directly tells us which block axis changed.
          const float ax = std::abs(static_cast<float>(diff.x));
          const float ay = std::abs(static_cast<float>(diff.y));
          const float az = std::abs(static_cast<float>(diff.z));
          if (ax >= ay && ax >= az && ax > 0.5f)
            face_normal = glm::vec3(static_cast<float>(diff.x), 0.0f, 0.0f);
          else if (ay >= ax && ay >= az && ay > 0.5f)
            face_normal = glm::vec3(0.0f, static_cast<float>(diff.y), 0.0f);
          else if (az > 0.5f)
            face_normal = glm::vec3(0.0f, 0.0f, static_cast<float>(diff.z));
        }
      }
      // Fallback: if step-back didn't produce a normal, use the radial up
      // direction (place block above the hit block on the sphere surface).
      if (glm::length(face_normal) < 0.1f) {
        face_normal = glm::normalize(local_player.transform.position);
      }
      targeted_face_normal_ = face_normal;

      // Handle left-click (break)
      if (input_frame.primary.left_click_pressed) {
        VoxelChunk &chunk = block_world_.get_or_generate_chunk(addr);
        chunk.set_material(addr.block.x, addr.block.y, addr.block.z, VoxelMaterial::Air);
        chunk.set_solid(addr.block.x, addr.block.y, addr.block.z, false);
        // Remove stale mesh from scene so it will be rebuilt next frame.
        const uint64_t mid = BlockWorld::chunk_mesh_id(addr);
        scene.opaque_meshes.erase(
            std::remove_if(scene.opaque_meshes.begin(), scene.opaque_meshes.end(),
                           [mid](const RenderMesh &m) { return m.mesh_id == mid; }),
            scene.opaque_meshes.end());
      }

      // Handle right-click (place)
      if (input_frame.primary.right_click_pressed) {
        // Place block at the neighbor position in the face normal direction.
        // Compute the world-space position adjacent to the hit face.
        const glm::dvec3 place_world = glm::dvec3(hit_block_world) +
            glm::dvec3(targeted_face_normal_) * block_world_.config().block_size;
        BlockAddress place_addr = block_world_.address_from_world(place_world);
        // Don't place inside the hit block itself.
        if (place_addr != addr) {
          VoxelChunk *place_chunk = &block_world_.get_or_generate_chunk(place_addr);
          if (place_chunk != nullptr &&
              place_chunk->material(place_addr.block.x, place_addr.block.y, place_addr.block.z) == VoxelMaterial::Air) {
            place_chunk->set_material(place_addr.block.x, place_addr.block.y, place_addr.block.z, VoxelMaterial::Stone);
            place_chunk->set_solid(place_addr.block.x, place_addr.block.y, place_addr.block.z, true);
            // Remove stale mesh for the affected chunk.
            const uint64_t mid = BlockWorld::chunk_mesh_id(place_addr);
            scene.opaque_meshes.erase(
                std::remove_if(scene.opaque_meshes.begin(), scene.opaque_meshes.end(),
                               [mid](const RenderMesh &m) { return m.mesh_id == mid; }),
                scene.opaque_meshes.end());
          }
        }
      }
    }
  }
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

  // ── Debug diagnostics ────────────────────────────────────────────────
  // WHY: voxel blocks were invisible despite chunks being generated and meshes
  // being built. These diagnostics trace the full pipeline: camera position,
  // snap origin, mesh count, and whether the first vertex projects to a
  // visible screen-space location. Logs every 60 frames to avoid spam.
  // TODO: remove once voxel rendering is confirmed working.
  if (frame_index % 60 == 0) {
    std::fprintf(stderr, "Frame %lu: Camera pos=(%.1f,%.1f,%.1f) snap=(%.1f,%.1f,%.1f) opaques=%zu chunks=%zu wireframes=%zu\n",
                 frame_index,
                 camera.transform.position.x, camera.transform.position.y, camera.transform.position.z,
                 camera_snap_origin_.x, camera_snap_origin_.y, camera_snap_origin_.z,
                 scene.opaque_meshes.size(), block_world_.chunk_count(),
                 scene.wireframe_meshes.size());

    // Project first vertex to NDC to verify it lands on screen.
    // WHY: confirms the camera-relative vertex offset + VP matrix produce
    // valid clip coordinates. NDC in [-1,1] with z in [0,1] = visible.
    if (!scene.opaque_meshes.empty() && !scene.opaque_meshes[0].vertices.empty()) {
      const auto &v0 = scene.opaque_meshes[0].vertices[0];
      const glm::vec3 rel_pos = v0.position; // already camera-relative
      const glm::vec3 world_pos = rel_pos + glm::vec3(camera_snap_origin_);
      const glm::mat4 p = camera.projection(16.0f / 9.0f);
      const glm::mat4 vp = p * camera.view();
      const glm::vec4 clip = vp * glm::vec4(world_pos, 1.0f);
      const glm::vec3 ndc = (clip.w != 0.0f) ? glm::vec3(clip) / clip.w : glm::vec3(999.0f);
      std::fprintf(stderr, "  Vertex0: world=(%.1f,%.1f,%.1f) clip=(%.2f,%.2f,%.2f,%.2f) ndc=(%.2f,%.2f,%.2f) %s\n",
                   world_pos.x, world_pos.y, world_pos.z,
                   clip.x, clip.y, clip.z, clip.w,
                   ndc.x, ndc.y, ndc.z,
                   (ndc.x >= -1 && ndc.x <= 1 && ndc.y >= -1 && ndc.y <= 1 && ndc.z >= 0 && ndc.z <= 1) ? "VISIBLE" : "CLIPPED");
    }
  }

  // ── Block world chunk streaming ─────────────────────────────────────
  // Load surface chunks in a radius around the player using the cube-sphere
  // shell architecture (6 sectors, radial shells doubling horizontal res,
  // 16^3 chunks). This follows proven techniques from Bowerbyte Blocky Planet,
  // Jordan Peck Voxel Planet, Jacco's shell+slab approach, and the 2016
  // Dimitrijević et al. paper on cube map projections for planet terrain
  // (quadsphere to minimize distortion at 1m surface resolution).
  // New chunks generated+meshed budget-limited per frame. When player moves
  // to new chunk center or snap drifts, stale evicted and full set rebuilt.
  // WHY for playable: only near-player data can be loaded (full 2000km 1m
  // surface is ~10^13 blocks impossible); dy layers ensure the visible
  // terrain top (Grass) in the outer thin shell is meshed (not just deep stone).
  double chunk_gen_ms = 0.0;
  double mesh_build_ms = 0.0;
  {
    const BlockAddress player_addr =
        block_world_.address_from_world(
            glm::dvec3(local_player.transform.position));
    const int32_t surface_shell = block_world_.shell_count() - 1;
    const int32_t chunk_radius = 3;  // 7x7 xz = 49 chunks per layer. Increased from 2 (25) for better surface coverage near player.

    // Hash the chunk center (incl. radial y) to detect player movement to a
    // new (x,z) column or crossing into a different radial chunk layer.
    uint64_t center_hash =
        (static_cast<uint64_t>(player_addr.chunk.x) << 32) ^
        (static_cast<uint64_t>(static_cast<uint32_t>(player_addr.chunk.z))) ^
        (static_cast<uint64_t>(static_cast<uint32_t>(player_addr.chunk.y)) << 16) ^
        (static_cast<uint64_t>(static_cast<uint8_t>(player_addr.sector)) << 48);
    const bool player_moved = (center_hash != last_chunk_center_hash_);

    // Collect desired chunk addresses for the surface shell.
    // Only load the player's current y-layer for performance.
    std::vector<BlockAddress> desired;
    const int32_t player_cy = player_addr.chunk.y;
    for (int32_t cz = -chunk_radius; cz <= chunk_radius; ++cz) {
      for (int32_t cx = -chunk_radius; cx <= chunk_radius; ++cx) {
        BlockAddress addr{};
        addr.sector = player_addr.sector;
        addr.shell  = surface_shell;
        addr.chunk  = glm::ivec3(
            player_addr.chunk.x + cx,
            player_cy,
            player_addr.chunk.z + cz);
        desired.push_back(addr);
      }
    }

    // Generate new chunks (budget-limited). Use modest budget to avoid frame spikes.
    // ── Frame profiler: time chunk generation (noise sampling + voxel data) ──
    const PerfClock::time_point chunk_gen_start = PerfClock::now();
    const uint32_t gen_budget = 8;
    std::vector<BlockAddress> new_chunks;
    for (const BlockAddress &addr : desired) {
      if (new_chunks.size() >= gen_budget) break;
      if (block_world_.find_chunk(addr) != nullptr) continue;
      block_world_.get_or_generate_chunk(addr);
      new_chunks.push_back(addr);
    }
    chunk_gen_ms = elapsed_ms(chunk_gen_start, PerfClock::now());

    // Rebuild full mesh set when player moves to new chunk center
    // or when new chunks are loaded, or when snap origin drifts.
    // NOTE: We capture snap_origin_dirty_ BEFORE resetting it because the
    // inner loop needs to know if all meshes should be rebuilt.
    const bool need_full_rebuild = player_moved || snap_origin_dirty_;
    // ── Frame profiler: time mesh building (face culling + greedy meshing) ──
    const PerfClock::time_point mesh_build_start = PerfClock::now();
    if (player_moved || !new_chunks.empty() || snap_origin_dirty_) {
      if (need_full_rebuild) {
        scene.opaque_meshes.clear();
        last_chunk_center_hash_ = center_hash;
        snap_origin_dirty_ = false;
      }

      // Build set of desired mesh_ids for this frame.
      std::unordered_set<uint64_t> desired_ids;
      // Track LOD distribution for debug HUD.
      uint32_t lod_distribution[4] = {0, 0, 0, 0};
      for (const BlockAddress &addr : desired) {
        // Compute stable mesh_id from address (sector+shell+chunk only).
        BlockAddress ck = addr;
        ck.block = glm::ivec3(0);
        const VoxelChunk *chunk = block_world_.find_chunk(ck);
        if (chunk == nullptr) continue;

        // ── LOD computation ─────────────────────────────────────────────
        // Compute chunk center in world space for screen-error metric.
        const glm::dvec3 chunk_center = block_world_.world_from_address(ck);
        const glm::dvec3 cam_pos = glm::dvec3(camera.transform.position);
        const float screen_h = std::max(1.0f, static_cast<float>(
            surface.height));
        // Chunk world size = chunk_size * block_size (16 * 1.0 = 16m).
        const double chunk_ws = static_cast<double>(block_world_.config().chunk_size) *
                                block_world_.config().block_size;
        const ChunkLOD clod = lod_system_.compute_lod(
            chunk_center, cam_pos, screen_h, chunk_ws);

        // Track LOD distribution.
        if (clod.level >= 0 && clod.level <= 3) {
          lod_distribution[clod.level]++;
        }

        uint64_t mid = BlockWorld::chunk_mesh_id(ck);
        desired_ids.insert(mid);

        // Only rebuild meshes for new chunks or when player moved
        // or when snap origin drifted.
        bool is_new = false;
        for (const auto &nc : new_chunks) {
          BlockAddress nk = nc; nk.block = glm::ivec3(0);
          if (nk == ck) { is_new = true; break; }
        }
        if (!need_full_rebuild && !is_new) continue;

        auto solid_at = [this, &ck, chunk](const BlockAddress &na) -> bool {
          BlockAddress nk = na;
          nk.block = glm::ivec3(0);
          const VoxelChunk *nc = (nk == ck) ? chunk : block_world_.find_chunk(nk);
          if (nc == nullptr) return false;
          return nc->solid(na.block.x, na.block.y, na.block.z);
        };

        RenderMesh mesh = block_world_.build_chunk_mesh(ck, *chunk, solid_at,
                                                         camera_snap_origin_,
                                                         clod.level);
        if (!mesh.vertices.empty()) {
          scene.opaque_meshes.push_back(std::move(mesh));
          // Debug: confirm mesh reaches the scene upload path.
          spdlog::debug("Mesh uploaded: mid={} verts={} idxs={} lod={}",
                        mid, mesh.vertices.size(), mesh.indices.size(), clod.level);
        } else {
          // Debug: mesh was built but has no geometry — all faces culled?
          spdlog::debug("Mesh EMPTY: mid={} lod={}", mid, clod.level);
        }
      }

      // Save LOD distribution to render stats for HUD display.
      for (int i = 0; i < 4; ++i) {
        render_stats.lod_chunk_count[i] = lod_distribution[i];
      }
      // Debug: log LOD distribution once to verify LOD system is working.
      // WHY: confirm LOD levels are computed and populated for 49 chunks.
      // TODO: remove once LOD system is confirmed working.
      static bool logged_lod = false;
      if (!logged_lod) {
        logged_lod = true;
        std::fprintf(stderr, "LOD distribution: L0=%u L1=%u L2=%u L3=%u\n",
                     lod_distribution[0], lod_distribution[1],
                     lod_distribution[2], lod_distribution[3]);
      }

      // Evict meshes no longer in the desired set (player moved away).
      if (player_moved) {
        auto &meshes = scene.opaque_meshes;
        meshes.erase(
            std::remove_if(meshes.begin(), meshes.end(),
                           [&desired_ids](const RenderMesh &m) {
                             return desired_ids.find(m.mesh_id) == desired_ids.end();
                           }),
            meshes.end());
      }
    }
    mesh_build_ms = elapsed_ms(mesh_build_start, PerfClock::now());
  }

  // Wireframe overlay — regenerate when dirty. Only show when devhud enabled.
  // WHY: the wireframe planet mesh (colored lines per cube face) overlays
  // the voxel terrain and creates confusing grid patterns. Disable for
  // normal gameplay; enable with F2 for debugging.
  if (wireframe_planet_dirty_) {
    wireframe_planet_mesh_ = build_wireframe_voxel_planet_mesh(
        wireframe_planet_, wireframe_planet_.chunks_per_face);
    wireframe_planet_dirty_ = false;
  }
  scene.wireframe_meshes.clear();
  if (session_state_.devhud_enabled) {
    scene.wireframe_meshes.push_back(wireframe_planet_mesh_);
  }

  // ── Solar system update ──────────────────────────────────────────────
  // Advance orbital simulation with frame time and build celestial body
  // meshes BEFORE upload_scene so they are uploaded to GPU this frame.
  // WHY before upload: celestial meshes must reach the GPU for the
  // current frame's render; after would cause a 1-frame lag.
  solar_system_time_ += frame_dt;
  solar_system_.update(solar_system_time_);

  // Update sun direction from solar system into atmosphere.
  // WHY: the planet orbits the sun (planet at calculated position,
  // voxel world centred at origin).  We offset the solar-system
  // positions so the planet-centre stays at world (0,0,0) for
  // block-world compatibility, while the sun appears to move.
  {
    const glm::dvec3 planet_orbit_pos = solar_system_.body_position(1); // planet
    const glm::dvec3 cam_world = glm::dvec3(camera.transform.position);
    const glm::dvec3 sun_planet_centric = solar_system_.sun_position() - planet_orbit_pos;
    const glm::dvec3 sun_dir = glm::normalize(sun_planet_centric - cam_world);
    atmosphere_.set_sun_direction(sun_dir);
  }

  // ── Celestial body rendering ────────────────────────────────────────
  // Strip previous frame's celestial meshes from the end of opaque_meshes,
  // then append new camera-relative sphere meshes for the current frame.
  {
    if (last_celestial_mesh_count_ > 0 &&
        last_celestial_mesh_count_ <= scene.opaque_meshes.size()) {
      scene.opaque_meshes.erase(
          scene.opaque_meshes.end() - static_cast<long>(last_celestial_mesh_count_),
          scene.opaque_meshes.end());
    }
    last_celestial_mesh_count_ = 0;

    const glm::dvec3 planet_orbit_pos = solar_system_.body_position(1);

    for (int32_t i = 0; i < solar_system_.body_count(); ++i) {
      const CelestialBody& body = solar_system_.bodies()[i];
      // Skip the planet body — the player is standing on the voxel planet.
      if (i == 1) continue;

      const glm::dvec3 body_world = body.position - planet_orbit_pos;
      const glm::vec3 rel_pos = camera_relative_position(
          body_world, scene.camera_origin);

      const float radius = static_cast<float>(body.orbital.radius);
      RenderMesh sphere = build_debug_sphere_mesh(rel_pos, radius, body.color);
      sphere.mesh_id = 0; // transient — always re-upload
      scene.opaque_meshes.push_back(std::move(sphere));
      ++last_celestial_mesh_count_;
    }
  }

  // ── Frame profiler: time GPU upload (buffer creation/update) ──
  const PerfClock::time_point gpu_upload_start = PerfClock::now();
  renderer.upload_scene(scene);
  const double gpu_upload_ms = elapsed_ms(gpu_upload_start, PerfClock::now());

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

  // ── Frame profiler: smooth per-stage timings ─────────────────────────
  // WHY: expose chunk gen, mesh build, and GPU upload costs for
  // bottleneck identification in the dev HUD (F2). Smoothing prevents
  // flickering numbers from frame-to-frame variance.
  render_stats.chunk_gen_ms =
      smooth_metric(render_stats.chunk_gen_ms, chunk_gen_ms);
  render_stats.mesh_build_ms =
      smooth_metric(render_stats.mesh_build_ms, mesh_build_ms);
  render_stats.gpu_upload_ms =
      smooth_metric(render_stats.gpu_upload_ms, gpu_upload_ms);

  // Accumulate vertex/index counts from chunk meshes for HUD display.
  // These represent application-level geometry for the voxel terrain.
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

  scene.debug_world = build_local_player_debug_mesh(
      local_player, local_player_animation, session_state_.devhud_enabled);
  refresh_overlay_text();
  renderer.update_dynamic_meshes(scene.debug_world, scene.debug_screen);

  // ── Atmosphere computation ──────────────────────────────────────────
  // Compute sky color for the camera view direction.  This is used as the
  // clear color (background sky).  The fragment shader applies aerial
  // perspective (transmittance) for terrain fragments.
  if (atmosphere_enabled_) {
    const glm::dvec3 cam_world_pos = glm::dvec3(camera.transform.position);
    const glm::dvec3 cam_dir = glm::dvec3(camera.forward());
    atmosphere_state_ = atmosphere_.compute_sky_color(cam_world_pos, cam_dir);
  }

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

  // ── Atmosphere uniforms for GPU ─────────────────────────────────────
  // WHY camera-relative: terrain vertices are stored as offsets from
  // camera_snap_origin_ for float32 precision.  Atmosphere params must
  // use the same coordinate frame so the fragment shader can compute
  // transmittance correctly.  Planet center at world (0,0,0) maps to
  // (-snap_origin) in camera-relative space.
  {
    const auto& atm = atmosphere_.params();
    const glm::vec3 rel_center = -glm::vec3(camera_snap_origin_);
    ctx.atmosphere.planet_center_radius = glm::vec4(rel_center,
        static_cast<float>(atm.planet_radius));
    ctx.atmosphere.atm_params_1 = glm::vec4(
        static_cast<float>(atm.atmosphere_height),
        static_cast<float>(atm.rayleigh_scale_height),
        static_cast<float>(atm.mie_scale_height),
        static_cast<float>(atm.mie_asymmetry));
    ctx.atmosphere.rayleigh_scatter = glm::vec4(
        glm::vec3(atm.rayleigh_scattering), 0.0f);
    ctx.atmosphere.mie_scatter = glm::vec4(
        static_cast<float>(atm.mie_scattering), 0.0f, 0.0f, 0.0f);
    ctx.atmosphere.sun_dir_intensity = glm::vec4(
        glm::vec3(atm.sun_direction),
        static_cast<float>(atm.sun_intensity));

    // Sky color for clear color (tone-map from HDR).
    // Simple Reinhard tone mapping: color / (1 + color).
    glm::vec3 sc = atmosphere_state_.sky_color;
    glm::vec3 tm = sc / (glm::vec3(1.0f) + sc);
    ctx.atmosphere.sky_color = glm::vec4(tm, 1.0f);
  }

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
  local_player.camera_rig.pitch = -10.0f;
  local_player.camera_rig.distance = 0.0f;
}

GuiMenu::Character Engine::preferred_character() const {
  return GuiMenu::Character::Capsule;
}

void Engine::update_first_person_camera(PlayerEntity &player,
                                        Camera &out_camera) {
  update_first_person_camera(player, player.transform.position, out_camera);
}

void Engine::update_first_person_camera(PlayerEntity &player,
                                         const glm::vec3 &render_position,
                                         Camera &out_camera) {
  // WHY use view override: The Camera class computes view from
  // yawPitchRoll which assumes world-Y-up. On a sphere planet the local "up"
  // varies by position. We compute the view matrix directly with glm::lookAt
  // in the local tangent frame, then set it as an override so the renderer
  // uses the correct orientation regardless of planet curvature.

  // Radial "up" from planet center.
  glm::vec3 local_up = glm::normalize(render_position);
  if (glm::length(local_up) < 0.1f) local_up = glm::vec3(0.0f, 1.0f, 0.0f);

  // Build a local tangent basis at the player position.
  // Use world +Y as reference for "north", with pole fallback to +Z.
  glm::vec3 world_ref = glm::vec3(0.0f, 1.0f, 0.0f);
  if (std::abs(glm::dot(local_up, world_ref)) > 0.99f) {
    world_ref = glm::vec3(0.0f, 0.0f, 1.0f);
  }
  const glm::vec3 east = glm::normalize(glm::cross(world_ref, local_up));
  const glm::vec3 north = glm::normalize(glm::cross(local_up, east));

  const float yaw_rad = glm::radians(player.camera_rig.yaw);
  const float pitch_rad = glm::radians(player.camera_rig.pitch);

  // Camera at player eye position: 1.6m above feet along radial up.
  constexpr float k_eye_height = 1.6f;
  const glm::vec3 eye_pos = render_position + local_up * k_eye_height;

  // View direction: yaw rotates in tangent plane, pitch tilts up/down.
  // Positive pitch = look up (away from surface), negative = look down.
  const glm::vec3 view_dir = glm::normalize(
      std::cos(pitch_rad) * (std::sin(yaw_rad) * east + std::cos(yaw_rad) * north)
      + std::sin(pitch_rad) * local_up);

  // Build view matrix from local tangent frame using glm::lookAt.
  // This correctly handles any position on the sphere — camera up is
  // always the local radial direction.
  const glm::mat4 view_matrix = glm::lookAt(
      glm::vec3(eye_pos),
      glm::vec3(eye_pos + view_dir),
      glm::vec3(local_up));

  out_camera.set_view_override(view_matrix);
  out_camera.transform.position = eye_pos;
  // euler_radians unused when view override is active, but keep in sync
  // for any debug display that reads them.
  out_camera.transform.euler_radians.y = std::atan2(view_dir.x, -view_dir.z);
  out_camera.transform.euler_radians.x =
      -std::asin(std::clamp(view_dir.y, -1.0f, 1.0f));
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
    // ── Crosshair ────────────────────────────────────────────────────
    // Draw a small white cross at screen center for first-person aiming.
    const glm::vec3 crosshair_color(0.95f, 0.95f, 0.95f);
    constexpr float ch_size = 0.018f;   // arm length in NDC
    constexpr float ch_thick = 0.003f;  // arm thickness in NDC
    // Horizontal bar
    append_screen_rect(scene.debug_screen,
        -ch_size, ch_thick, ch_size, -ch_thick, crosshair_color);
    // Vertical bar
    append_screen_rect(scene.debug_screen,
        -ch_thick, ch_size, ch_thick, -ch_size, crosshair_color);
    return;
  }

  const ProfilingSnapshot &profiling = render_stats.profiling;

  // Get player block address for debug display.
  const BlockAddress player_dbg_addr =
      block_world_.address_from_world(
          glm::dvec3(local_player.transform.position));

  std::string overlay_text =
      "FPS: " + std::to_string(static_cast<int>(render_stats.fps + 0.5)) +
      "\nCPU: " + std::to_string(render_stats.cpu_ms).substr(0, 5) +
      " ms  Frame: " + std::to_string(render_stats.frame_ms).substr(0, 5) +
      " ms" +
      "\nRender: " + std::to_string(render_stats.render_cpu_ms).substr(0, 5) +
      " ms  Fixed: " + std::to_string(render_stats.fixed_cpu_ms).substr(0, 5) +
      " ms x" + std::to_string(render_stats.fixed_steps) +
      "\nChunk gen: " + std::to_string(render_stats.chunk_gen_ms).substr(0, 5) +
      " ms  Mesh build: " + std::to_string(render_stats.mesh_build_ms).substr(0, 5) +
      " ms" +
      "\nGPU upload: " + std::to_string(render_stats.gpu_upload_ms).substr(0, 5) +
      " ms  Draw calls: " + std::to_string(render_stats.draw_call_count) +
      "\nVertices: " + std::to_string(render_stats.total_vertices) +
      "  Indices: " + std::to_string(render_stats.total_indices) +
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
  // LOD distribution: how many chunks at each level.
  overlay_text += "\nLOD: L0=" + std::to_string(render_stats.lod_chunk_count[0]) +
      " L1=" + std::to_string(render_stats.lod_chunk_count[1]) +
      " L2=" + std::to_string(render_stats.lod_chunk_count[2]) +
      " L3=" + std::to_string(render_stats.lod_chunk_count[3]);

  // Player position and block address debug info.
  overlay_text += "\nPos: (" +
      std::to_string(static_cast<int>(local_player.transform.position.x)) + ", " +
      std::to_string(static_cast<int>(local_player.transform.position.y)) + ", " +
      std::to_string(static_cast<int>(local_player.transform.position.z)) + ")";
  overlay_text += "\nSector: " + std::to_string(static_cast<int>(player_dbg_addr.sector)) +
      " Shell: " + std::to_string(player_dbg_addr.shell);
  overlay_text += "\nChunk: (" + std::to_string(player_dbg_addr.chunk.x) + ", " +
      std::to_string(player_dbg_addr.chunk.y) + ", " +
      std::to_string(player_dbg_addr.chunk.z) + ")";
  overlay_text += "\nOpaques: " + std::to_string(scene.opaque_meshes.size());

  // ── Solar system HUD info ───────────────────────────────────────────
  overlay_text += "\nSolar system: ";
  overlay_text += std::to_string(solar_system_.body_count()) + " bodies";
  overlay_text += "\nSun dir: (" +
      std::to_string(static_cast<int>(atmosphere_.params().sun_direction.x * 100.0) / 100.0) + ", " +
      std::to_string(static_cast<int>(atmosphere_.params().sun_direction.y * 100.0) / 100.0) + ", " +
      std::to_string(static_cast<int>(atmosphere_.params().sun_direction.z * 100.0) / 100.0) + ")";
  {
    const glm::dvec3 planet_pos = solar_system_.body_position(1);
    overlay_text += "\nPlanet orbit pos: (" +
        std::to_string(static_cast<int>(planet_pos.x)) + ", " +
        std::to_string(static_cast<int>(planet_pos.y)) + ", " +
        std::to_string(static_cast<int>(planet_pos.z)) + ")";
  }
  overlay_text += "\nSolar time: " + std::to_string(static_cast<int>(solar_system_time_)) + "s";

  // ── Atmosphere HUD info ─────────────────────────────────────────────
  overlay_text += "\nAtmosphere: ";
  overlay_text += atmosphere_enabled_ ? "ON" : "OFF";
  overlay_text += "\nSky color: (" +
      std::to_string(atmosphere_state_.sky_color.r).substr(0, 5) + ", " +
      std::to_string(atmosphere_state_.sky_color.g).substr(0, 5) + ", " +
      std::to_string(atmosphere_state_.sky_color.b).substr(0, 5) + ")";
  overlay_text += "\nSun cos(angle): " + std::to_string(atmosphere_state_.sun_angle_cos).substr(0, 5);

  // Camera debug info for visual debugging.
  const glm::vec3 local_up = glm::normalize(local_player.transform.position);
  overlay_text += "\nCam pitch: " + std::to_string(static_cast<int>(local_player.camera_rig.pitch)) +
      " yaw: " + std::to_string(static_cast<int>(local_player.camera_rig.yaw));
  overlay_text += "\nCam dist: " + std::to_string(static_cast<int>(local_player.camera_rig.distance));
  overlay_text += "\nLocal up: (" +
      std::to_string(local_up.x).substr(0, 4) + ", " +
      std::to_string(local_up.y).substr(0, 4) + ", " +
      std::to_string(local_up.z).substr(0, 4) + ")";
  overlay_text += "\nTerrain h: " + std::to_string(
      block_world_.terrain_height_at(glm::dvec3(local_up)));

  // Gameplay state debug info.
  overlay_text += "\nGrounded: " + std::string(local_player.controller.grounded ? "YES" : "NO");
  overlay_text += "\nVel: " + std::to_string(static_cast<int>(
      glm::length(local_player.controller.velocity)));
  overlay_text += "\nLocomotion: " + std::string(
      player_locomotion_state_name(local_player.locomotion.state));

  // ── Flight vehicle HUD ──────────────────────────────────────────────
  if (flight_vehicle_spawned_) {
    const auto& vs = flight_vehicle_.state();
    const auto& vf = flight_vehicle_.forces();
    overlay_text += "\n--- Vehicle ---";
    overlay_text += "\nEngine: " + std::string(vs.engine_active ? "ON (T)" : "OFF (T)");
    overlay_text += "\nThrottle: " + std::to_string(static_cast<int>(vs.throttle * 100)) + "%";
    overlay_text += "\nFuel: " + std::to_string(static_cast<int>(vs.fuel_remaining)) + "kg";
    overlay_text += "\nSpeed: " + std::to_string(static_cast<int>(glm::length(vs.velocity))) + "m/s";
    overlay_text += "\nAltitude: " + std::to_string(static_cast<int>(
        glm::length(vs.position) - 500.0)) + "m";
    overlay_text += "\nAtmo: " + std::string(flight_vehicle_.in_atmosphere() ? "YES" : "SPACE");
    // Force magnitudes in kN for readability.
    const auto kn = [](double n) -> int { return static_cast<int>(n / 1000.0); };
    overlay_text += "\nThrust: " + std::to_string(kn(glm::length(vf.thrust))) + "kN";
    overlay_text += "\nLift: " + std::to_string(kn(glm::length(vf.lift))) + "kN";
    overlay_text += "\nDrag: " + std::to_string(kn(glm::length(vf.drag))) + "kN";
  }

  // Block targeting debug info.
  if (targeted_addr_.has_value()) {
    const BlockAddress &ta = *targeted_addr_;
    overlay_text += "\nTarget block: (" + std::to_string(ta.block.x) + "," +
        std::to_string(ta.block.y) + "," + std::to_string(ta.block.z) + ")";
    overlay_text += "\nFace normal: (" +
        std::to_string(static_cast<int>(targeted_face_normal_.x)) + "," +
        std::to_string(static_cast<int>(targeted_face_normal_.y)) + "," +
        std::to_string(static_cast<int>(targeted_face_normal_.z)) + ")";
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
