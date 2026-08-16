#include "engine/engine.hpp"
#include "engine/planet_gameplay_config.hpp"

#include "engine_core/memory.hpp"
#include "engine_core/timing.hpp"
#include "engine_presentation/debug_scene_builder.hpp"
#include "engine_gameplay/player/player_visuals.hpp"
#include "engine_gameplay/player/surface_orientation.hpp"
#include "engine_render/debug_draw/debug_draw.hpp"
#include "engine_render/debug_text.hpp"
#include "engine_world/wireframe_planet.hpp"
#include "engine_world/world_gen.hpp"
#include "engine_world/vegetation.hpp"
#include "engine_world/planet.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <limits>
#include <numbers>
#include <string>
#include <unordered_set>

namespace {
using PerfClock = std::chrono::steady_clock;

constexpr uint64_t kGlobalPlanetSurfaceMeshId =
    0x504c414e45544c4full; // "PLANETLO"
#if defined(VOXOV_PLATFORM_ANDROID) || defined(VOXOV_PLATFORM_WEB)
constexpr int32_t kSurfaceStreamRadiusChunks = 2;
constexpr uint32_t kChunkGenerationBudget = 1;
constexpr uint32_t kChunkMeshBudget = 1;
#else
constexpr int32_t kSurfaceStreamRadiusChunks = 2;
constexpr uint32_t kChunkGenerationBudget = 2;
constexpr uint32_t kChunkMeshBudget = 2;
#endif

double elapsed_ms(const PerfClock::time_point &start,
                  const PerfClock::time_point &end) {
  return std::chrono::duration<double, std::milli>(end - start).count();
}

BlockAddress chunk_address_key(const BlockAddress &addr) {
  BlockAddress key = addr;
  key.block = glm::ivec3(0);
  return key;
}

void collect_remesh_targets(const BlockWorld &world,
                            const std::vector<BlockAddress> &new_chunks,
                            const std::vector<BlockAddress> &desired,
                            std::unordered_set<uint64_t> &out) {
  std::unordered_set<uint64_t> desired_ids;
  desired_ids.reserve(desired.size());
  for (const BlockAddress &addr : desired) {
    desired_ids.insert(BlockWorld::chunk_mesh_id(chunk_address_key(addr)));
  }

  for (const BlockAddress &nc : new_chunks) {
    const BlockAddress base = chunk_address_key(nc);
    out.insert(BlockWorld::chunk_mesh_id(base));
    for (int32_t dcy = -1; dcy <= 1; ++dcy) {
      for (int32_t dcz = -1; dcz <= 1; ++dcz) {
        for (int32_t dcx = -1; dcx <= 1; ++dcx) {
          if (dcx == 0 && dcy == 0 && dcz == 0) {
            continue;
          }
          BlockAddress neighbor{};
          if (!world.offset_chunk_address(base, dcx, dcy, dcz, neighbor)) {
            continue;
          }
          const uint64_t mid = BlockWorld::chunk_mesh_id(neighbor);
          if (desired_ids.find(mid) != desired_ids.end()) {
            out.insert(mid);
          }
        }
      }
    }
  }
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

void append_screen_cursor(RenderMesh &dst, const glm::vec2 &position,
                          const glm::vec3 &color) {
  constexpr float kArm = 0.014f;
  constexpr float kThickness = 0.0015f;
  append_screen_rect(dst, position.x - kArm, position.y - kThickness,
                     position.x + kArm, position.y + kThickness, color);
  append_screen_rect(dst, position.x - kThickness, position.y - kArm,
                     position.x + kThickness, position.y + kArm, color);
}

void append_wire_line(RenderMesh &mesh, const glm::dvec3 &a,
                      const glm::dvec3 &b, const glm::vec3 &color) {
  mesh.vertices.push_back(RenderVertex{glm::vec3(a), color, glm::vec3(0.0f)});
  mesh.vertices.push_back(RenderVertex{glm::vec3(b), color, glm::vec3(0.0f)});
  mesh.indices.push_back(static_cast<uint32_t>(mesh.vertices.size() - 2));
  mesh.indices.push_back(static_cast<uint32_t>(mesh.vertices.size() - 1));
}

void append_dashed_line(RenderMesh &mesh, const glm::dvec3 &start,
                        const glm::dvec3 &end, const glm::vec3 &color,
                        int32_t segments) {
  const int32_t count = std::max(4, segments);
  for (int32_t i = 0; i < count; i += 2) {
    const double t0 = static_cast<double>(i) / static_cast<double>(count);
    const double t1 = static_cast<double>(i + 1) / static_cast<double>(count);
    append_wire_line(mesh, glm::mix(start, end, t0),
                     glm::mix(start, end, t1), color);
  }
}

void append_dashed_ring(RenderMesh &mesh, const glm::dvec3 &center,
                        const glm::dvec3 &right, const glm::dvec3 &up,
                        double radius, double rotation,
                        const glm::vec3 &color) {
  constexpr int32_t kSegments = 32;
  const double pi = std::numbers::pi_v<double>;
  for (int32_t i = 0; i < kSegments; i += 2) {
    const double a0 = rotation + 2.0 * pi * static_cast<double>(i) /
                      static_cast<double>(kSegments);
    const double a1 = rotation + 2.0 * pi * static_cast<double>(i + 1) /
                      static_cast<double>(kSegments);
    const glm::dvec3 p0 = center +
        (right * std::cos(a0) + up * std::sin(a0)) * radius;
    const glm::dvec3 p1 = center +
        (right * std::cos(a1) + up * std::sin(a1)) * radius;
    append_wire_line(mesh, p0, p1, color);
  }
}

bool is_sky_navigation_target(int32_t body_index) {
  // Body 1 is the local voxel planet and is not a destination in its own sky.
  return body_index != 1;
}

std::string uppercase_ascii(std::string value) {
  for (char &character : value) {
    character = static_cast<char>(std::toupper(
        static_cast<unsigned char>(character)));
  }
  return value;
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
  if (!devhud_enabled) {
    return RenderMesh{};
  }
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
  planet_def.radius = kPlayablePlanetConfig.radius_m;
  planet_def.voxel_size = kPlayablePlanetConfig.voxel_size_m;
  planet_def.chunks_per_face = 64;
  planet_def.seed = k_voxov_flat_world_seed;

  BlockWorldConfig bw_cfg{};
  bw_cfg.planet = planet_def;
  bw_cfg.surface_shells = kPlayablePlanetConfig.surface_shells;
  bw_cfg.base_resolution = kPlayablePlanetConfig.base_resolution;
  bw_cfg.block_size = kPlayablePlanetConfig.voxel_size_m;
  bw_cfg.terrain_feature_size =
      kPlayablePlanetConfig.terrain_feature_size_m;
  bw_cfg.terrain_base_height =
      kPlayablePlanetConfig.terrain_base_height_blocks;
  bw_cfg.terrain_amplitude =
      kPlayablePlanetConfig.terrain_amplitude_blocks;
  bw_cfg.terrain_min_height =
      kPlayablePlanetConfig.terrain_min_height_blocks;
  bw_cfg.terrain_max_height =
      kPlayablePlanetConfig.terrain_max_height_blocks;
  bw_cfg.terrain_shell_margin =
      kPlayablePlanetConfig.terrain_shell_margin_blocks;
  bw_cfg.chunk_size = kPlayablePlanetConfig.chunk_size;
  bw_cfg.seed = k_voxov_flat_world_seed;
  block_world_.init(bw_cfg);

  // Build the nearby companion's terrain runtime up front. It shares the
  // generator contract with Voxov but has its own radius, seed, chunk cache,
  // and collision surface so an arrival can become a real landing rather than
  // stopping at a rendered sphere.
  {
    BlockWorldConfig aster_cfg = bw_cfg;
    aster_cfg.planet.radius = 32.0;
    aster_cfg.planet.seed = k_voxov_flat_world_seed ^ 0xa57e'c0deull;
    aster_cfg.seed = aster_cfg.planet.seed;
    aster_block_world_.init(aster_cfg);
    aster_collision_world.set_planet_surface_collider(
        glm::vec3(0.0f), static_cast<float>(aster_cfg.planet.radius),
        static_cast<float>(aster_block_world_.max_surface_height_above_base()),
        [this](glm::vec3 direction) -> float {
          return static_cast<float>(
              aster_block_world_.surface_height_above_base(
                  glm::dvec3(direction)));
        });
  }
  chunk_generation_budget_ = kChunkGenerationBudget;
  mesh_build_budget_ = kChunkMeshBudget;

  // ── LOD system ──────────────────────────────────────────────────────────
  // WHY: distance-based chunk resolution reduces GPU vertex count for distant
  // chunks. screen-space error metric with hysteresis prevents popping.
  {
    LODConfig lod_cfg{};
    lod_cfg.error_threshold = 4.0f;      // pixels
    lod_cfg.hysteresis_factor = 1.5f;    // dead zone to prevent oscillation
    // A 64 m planet only keeps a small detailed patch. Coarsening those chunks
    // saved little while introducing unstitched mixed-stride borders.
    lod_cfg.max_lod_level = 0;
    lod_system_.init(lod_cfg);
  }

  // Wireframe debug overlay.
  wireframe_planet_ = planet_def;
  wireframe_planet_.chunks_per_face = 64;
  wireframe_planet_dirty_ = true;
  atmosphere_wireframe_mesh_ = build_atmosphere_wireframe_mesh(
      planet_def, kPlayablePlanetConfig.atmosphere_height_m);

  // Planet-surface collision from block-world terrain (shell-mapped meters).
  collision_world.set_planet_surface_collider(
      glm::vec3(planet_def.center),
      static_cast<float>(planet_def.radius),
      static_cast<float>(block_world_.max_surface_height_above_base()),
      [&bw = block_world_](glm::vec3 direction) -> float {
        return static_cast<float>(
            bw.surface_height_above_base(glm::dvec3(direction)));
      });

  // Player spawn on the sun-facing +X equator of the outer shell.
  {
    const int32_t shell = block_world_.shell_count() - 1;
    const int32_t equator_col =
        block_world_.shell_config(shell).horizontal_res / 2;
    local_player = PlayerControllerSystem::spawn_on_planet_surface(
        block_world_, collision_world, PlanetFace::PosX, equator_col,
        equator_col, 2.0);
    local_player.controller.capsuleRadius = 0.7f;
  }
  local_player.camera_rig.pitch = -45.0f;   // steeper angle to see terrain height variation
  local_player.camera_rig.distance = 0.0f;   // first-person: no orbit distance
  local_player.camera_rig.maxDistance = 0.0f;
  local_player.camera_rig.minDistance = 0.0f;
  local_player.camera_rig.pivotHeight = 0.0f;
  local_player.locomotion_tuning.flight_speed =
      kPlayablePlanetConfig.debug_flight_speed_mps;
  local_player.locomotion_tuning.flight_sprint_base_speed =
      kPlayablePlanetConfig.debug_flight_sprint_base_mps;
  local_player.locomotion_tuning.flight_sprint_altitude_scale =
      kPlayablePlanetConfig.debug_flight_sprint_altitude_scale;
  local_player.locomotion_tuning.flight_sprint_max_speed =
      kPlayablePlanetConfig.debug_flight_sprint_max_mps;
  local_player.locomotion_tuning.flight_acceleration =
      kPlayablePlanetConfig.debug_flight_acceleration_mps2;
  local_player.locomotion_tuning.flight_atmosphere_height =
      static_cast<float>(kPlayablePlanetConfig.atmosphere_height_m);
  local_player_prev_position = local_player.transform.position;
  local_player_animation.reset(local_player.anim_state);
  expedition_mission_.reset();
  camera.z_far = 512.0f;
  camera.z_near = 0.25f;
  update_first_person_camera(local_player, camera);

  scene.debug_world = build_local_player_debug_mesh(
      local_player, local_player_animation, session_state_.devhud_enabled);
  refresh_overlay_text();

    // Start with fly mode OFF — gravity walks on the sphere surface.
    debug_fly_mode_ = false;
    if (std::getenv("VOXOV_CAPTURE_ORBIT") != nullptr) {
      const glm::dvec3 observation_direction = glm::normalize(
          glm::dvec3(local_player.transform.position) - planet_def.center);
      local_player.transform.position = glm::vec3(
          planet_def.center + observation_direction *
                                  (planet_def.radius +
                                   kPlayablePlanetConfig.capture_orbit_altitude_m));
      local_player.camera_rig.pitch = -89.0f;
      local_player_prev_position = local_player.transform.position;
      debug_fly_mode_ = true;
    } else if (std::getenv("VOXOV_CAPTURE_FLIGHT") != nullptr) {
      const glm::dvec3 observation_direction = glm::normalize(
          glm::dvec3(local_player.transform.position) - planet_def.center);
      local_player.transform.position = glm::vec3(
          planet_def.center + observation_direction *
                                  (planet_def.radius +
                                   kPlayablePlanetConfig.capture_flight_altitude_m));
      // Look back toward the compact planet so automated flight captures
      // exercise the terrain handoff instead of recording empty sky.
      local_player.camera_rig.pitch = -89.0f;
      local_player_prev_position = local_player.transform.position;
      debug_fly_mode_ = true;
    }

    // ── Solar system initialization ───────────────────────────────────
    // Keplerian orbits use planetary distances. The planet's orbital position
    // determines sun direction; its block-world centre remains at the origin.
    solar_system_.init(planet_def.radius);
    solar_system_time_ = 0.0;

    // ── Coordinate frame manager initialization ───────────────────────
    // Set up hierarchical frame transforms for inter-planetary travel.
    // Player starts on planet surface → Planet frame.
    frame_manager_.init();
    active_frame_ = CoordinateFrame::Planet;
    active_body_index_ = 1;  // Voxov planet
    active_frame_label_ = "Planet";
    frame_transition_cooldown_ = 0.0;

    // ── Atmosphere initialization ────────────────────────────────────────
    // Rayleigh + Mie scattering for sky color and aerial perspective.
    // Keep the atmosphere in physical meters. This remains stable when the
    // local terrain streamer later becomes one body in a multi-body universe.
    {
        AtmosphereParams atm_params{};
        atm_params.planet_radius = planet_def.radius;
        atm_params.atmosphere_height =
            kPlayablePlanetConfig.atmosphere_height_m;
        atm_params.rayleigh_scattering = glm::dvec3(5.8e-6, 13.5e-6, 33.1e-6);
        atm_params.mie_scattering = 21.0e-5;
        atm_params.rayleigh_scale_height = 8'000.0;
        atm_params.mie_scale_height = 1'200.0;
        atm_params.mie_asymmetry = 0.76;
        // Sun direction: initialise from solar system (planet at orbital pos at t=0).
        atm_params.sun_direction = solar_system_.sun_direction_from(glm::dvec3(0.0));
        atm_params.sun_intensity = 20.0;
        atm_params.view_ray_samples = 12;
        atm_params.light_ray_samples = 6;
        atmosphere_.init(atm_params);
        atmosphere_enabled_ = kPlayablePlanetConfig.atmosphere_enabled;
    }

    spdlog::info("Engine init: block planet r={:.0f}m, shells={}, fly=OFF",
               planet_def.radius,
               block_world_.shell_count());
    // ── Flight vehicle spawn ──────────────────────────────────────────────
    // Spawn the vehicle on the planet surface near the player, with
    // initial forward direction pointing east (tangent to sphere).
    if (kPlayablePlanetConfig.flight_vehicle_enabled) {
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
        const double surface_r =
            block_world_.surface_radial_distance(surface_normal);
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
    } else {
        flight_vehicle_spawned_ = false;
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
  rebuild_global_planet_surface();
  load_persistent_game();
  update_first_person_camera(local_player, camera);

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
  interest.body_id = static_cast<uint32_t>(active_body_index_);
  interest.radius = 2;
  net_client_.set_chunk_interest(interest);
  return EngineConnectResult::Connected;
}

void Engine::start_local_server(uint16_t port, bool loopback_only) {
  if (local_server_running_ && local_server_loopback_ == loopback_only) {
    return;
  }
  if (local_server_running_) {
    local_server_.shutdown();
    local_server_running_ = false;
  }
  if (!local_server_.init(port, loopback_only)) {
    spdlog::error("Failed to start {} server on {}",
                  loopback_only ? "local-only" : "LAN", port);
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

void Engine::tick(double frame_dt,
                  EngineInputFrame input_frame,
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
  if (surface.width > 0 && surface.height > 0) {
    sky_navigation_cursor_ndc_ = glm::vec2(
        gameplay_input.cursor_position.x /
                static_cast<float>(surface.width) * 2.0f -
            1.0f,
        1.0f - gameplay_input.cursor_position.y /
                  static_cast<float>(surface.height) * 2.0f);
  }
  if (gameplay_input.sky_navigation_toggle_pressed &&
      session_state_.gameplay_started && !session_state_.menu_open) {
    sky_navigation_mode_ = !sky_navigation_mode_;
    sky_navigation_locked_ = false;
    last_hud_message_ = sky_navigation_mode_
        ? "Sky navigation ON — arrows select, ENTER locks"
        : "Sky navigation OFF";
  }

  if (sky_navigation_mode_) {
    const int32_t body_count = solar_system_.body_count();
    auto valid_target = [&](int32_t index) {
      return index >= 0 && index < body_count &&
             is_sky_navigation_target(index);
    };
    if (!valid_target(sky_navigation_target_index_)) {
      sky_navigation_target_index_ = body_count > 0 ? 0 : -1;
    }

    if (!sky_navigation_locked_ && body_count > 0 && surface.width > 0 &&
        surface.height > 0) {
      // Sky destinations are selected by hovering their projected marker.
      // Keep the selection radius generous enough for a moving target while
      // still requiring the cursor to be meaningfully over a destination.
      const glm::mat4 vp = camera.projection(
                               static_cast<float>(surface.width) /
                               static_cast<float>(surface.height)) *
                           camera.view();
      const glm::dvec3 planet_orbit_pos =
          solar_system_.body_position(active_body_index_);
      constexpr float kSelectionRadius = 0.14f;
      float best_distance_sq = kSelectionRadius * kSelectionRadius;
      int32_t hovered_target = -1;

      for (int32_t i = 0; i < body_count; ++i) {
        if (!valid_target(i)) continue;
        const CelestialBody &body =
            solar_system_.bodies()[static_cast<size_t>(i)];
        const glm::dvec3 body_world = body.position - planet_orbit_pos;
        glm::vec4 clip = vp * glm::vec4(glm::vec3(body_world), 1.0f);
        if (clip.w <= 0.0f) continue;
        const glm::vec2 marker(clip.x / clip.w, clip.y / clip.w);
        if (marker.x < -1.0f || marker.x > 1.0f || marker.y < -1.0f ||
            marker.y > 1.0f) {
          continue;
        }
        const glm::vec2 delta = marker - sky_navigation_cursor_ndc_;
        const float distance_sq = glm::dot(delta, delta);
        if (distance_sq < best_distance_sq) {
          best_distance_sq = distance_sq;
          hovered_target = i;
        }
      }
      if (hovered_target >= 0) {
        sky_navigation_target_index_ = hovered_target;
      }
    }

    if (gameplay_input.sky_navigation_lock_pressed &&
        valid_target(sky_navigation_target_index_)) {
      sky_navigation_locked_ = !sky_navigation_locked_;
      if (sky_navigation_locked_) {
        expedition_mission_.on_course_locked(sky_navigation_target_index_);
        (void)save_persistent_game();
      }
      last_hud_message_ = sky_navigation_locked_
          ? "Navigation locked"
          : "Navigation unlocked";
    }

    // Enter belongs to the sky selector while it is open. Cursor movement is
    // intentionally not forwarded to first-person camera look.
    disable_gameplay_actions(gameplay_input);
    gameplay_input.look_delta = glm::vec2(0.0f);
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

  // Holding W after locking a destination engages a readable flight assist:
  // the camera slews onto the dashed route, propulsion follows that route,
  // and arrival hands control to the destination's resident terrain runtime.
  // Releasing W returns control to the normal flight input.
  if (sky_navigation_mode_ && sky_navigation_locked_ &&
      gameplay_input.key_w &&
      sky_navigation_target_index_ >= 0 &&
      sky_navigation_target_index_ < solar_system_.body_count()) {
    const glm::dvec3 active_origin =
        solar_system_.body_position(active_body_index_);
    const CelestialBody &target = solar_system_.bodies()[static_cast<size_t>(
        sky_navigation_target_index_)];
    const glm::dvec3 target_local = target.position - active_origin;
    const glm::dvec3 to_target = target_local -
        glm::dvec3(local_player.transform.position);
    const double distance = glm::length(to_target);

    if (distance <= target.orbital.radius + 4.0 &&
        sky_navigation_target_index_ == 3) {
      const glm::dvec3 approach = glm::length(glm::dvec3(
          local_player.transform.position) - target_local) > 1.0e-6
          ? glm::normalize(glm::dvec3(local_player.transform.position) -
                           target_local)
          : glm::dvec3(1.0, 0.0, 0.0);
      switch_active_planet(sky_navigation_target_index_);
      local_player.transform.position = glm::vec3(
          approach * block_world_.surface_radial_distance(approach) +
          approach * 2.0);
      local_player.controller.velocity = glm::vec3(0.0f);
      local_player.controller.grounded = true;
      local_player.flight = PlayerFlightState{};
      debug_fly_mode_ = false;
      sky_navigation_locked_ = false;
      expedition_mission_.on_landed(active_body_index_);
      (void)save_persistent_game();
      last_hud_message_ = "Landed on " + target.name;
    } else {
      debug_fly_mode_ = true;
      gameplay_input.move = glm::vec2(0.0f, 1.0f);
      gameplay_input.sprint_held = true;

      const glm::vec3 up = collision_world.planet_up_at(
          local_player.transform.position);
      const glm::vec3 north =
          player_surface_orientation::reference_forward(
              local_player.camera_rig, up);
      const glm::vec3 east =
          player_surface_orientation::east_from_forward(north, up);
      const glm::vec3 direction = glm::normalize(glm::vec3(to_target));
      const float tangent_length = std::max(
          1.0e-5f, glm::length(direction - up * glm::dot(direction, up)));
      const float target_yaw = glm::degrees(std::atan2(
          glm::dot(direction, east), glm::dot(direction, north)));
      const float target_pitch = glm::degrees(std::atan2(
          glm::dot(direction, up), tangent_length));
      const float yaw_delta = std::remainder(
          target_yaw - local_player.camera_rig.yaw, 360.0f);
      const float steer = static_cast<float>(std::clamp(
          frame_dt * 5.0, 0.0, 1.0));
      local_player.camera_rig.yaw += yaw_delta * steer;
      local_player.camera_rig.pitch = glm::mix(
          local_player.camera_rig.pitch, target_pitch, steer);
    }
  }
  touch_controls_visible_ = input_frame.touch_mode;

  PlayerControllerSystem::update_camera_rig(
      local_player, gameplay_input, input_frame.touch_mode,
      static_cast<float>(frame_dt));

  ProfilingSnapshot profiling_sample{};
  bool jump_consumed = false;
  const RuntimeGameSessionCallbacks callbacks{
      .pump_server = [this]() {
        if (local_server_running_) {
          local_server_.pump();
        }
        net_client_.pump();
      },
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
            if (flight_vehicle_spawned_ &&
                flight_vehicle_.state().engine_active) {
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
              const double planet_radius = block_world_.planet().radius;
              constexpr double k_surface_gravity = 9.81;
              const double grav_mag = k_surface_gravity *
                  (planet_radius / dist_from_center) *
                  (planet_radius / dist_from_center);
              const glm::dvec3 gravity = grav_dir * grav_mag;

              // Air density: exponential decay with altitude.
              // Scale height H ≈ 20m for our small 500m planet.
              const double altitude = dist_from_center - planet_radius;
              constexpr double k_sea_level_density = 1.225;  // kg/m³ at surface
              constexpr double k_scale_height = 8'000.0;      // m
              const double air_density = (altitude > 0.0)
                  ? k_sea_level_density * std::exp(-altitude / k_scale_height)
                  : k_sea_level_density;

              flight_vehicle_.update(step.dt, air_density, gravity);
            } else if (flight_vehicle_spawned_ &&
                       gameplay_input.engine_toggle_pressed) {
              flight_vehicle_.state().engine_active =
                  !flight_vehicle_.state().engine_active;
              last_hud_message_ = flight_vehicle_.state().engine_active
                  ? "Vehicle engine ON" : "Vehicle engine OFF";
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
  // First-person camera from player eye position.
  update_first_person_camera(local_player, local_player.transform.position,
                              camera);

  // Preserve precision near blocks while allowing the whole compact planet
  // to fit in the frustum during debug flight.
  const double camera_altitude = std::max(
      0.0, glm::length(glm::dvec3(camera.transform.position) -
                       block_world_.planet().center) -
               block_world_.planet().radius);
  const float target_near = camera_altitude > 10'000.0
                                ? static_cast<float>(std::clamp(
                                      camera_altitude / 100'000.0,
                                      2.0, 100.0))
                                : 0.25f;
  const float target_far = static_cast<float>(std::max(
      512.0, std::min(block_world_.planet().radius * 12.0,
                      (block_world_.planet().radius + camera_altitude) * 4.0)));
  const float camera_ease = 1.0f - std::exp(
      -6.0f * static_cast<float>(std::clamp(frame_dt, 0.0, 0.1)));
  camera.z_near = glm::mix(camera.z_near, target_near, camera_ease);
  camera.z_far = target_far > camera.z_far
                     ? target_far
                     : glm::mix(camera.z_far, target_far, camera_ease);
  const float flight_speed = glm::length(local_player.controller.velocity);
  sky_navigation_aspect_ratio_ = static_cast<float>(surface.width) /
                                 static_cast<float>(std::max(1, surface.height));
  if (sky_navigation_mode_) {
    // Keep the star and distant planetary markers in the navigation frustum.
    camera.z_far = std::max(camera.z_far, 250'000'000.0f);
  }
  const float flight_fov_speed = std::max(
      1.0f, kPlayablePlanetConfig.debug_flight_sprint_max_mps);
  const float speed_fov = debug_fly_mode_
                              ? std::clamp(flight_speed / flight_fov_speed,
                                           0.0f, 1.0f) * 12.0f
                              : 0.0f;
  camera.fov_y_radians = glm::mix(
      camera.fov_y_radians, glm::radians(70.0f + speed_fov), camera_ease);

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
      // Reject samples far from the planet's editable surface shell. The old
      // fixed 2 km limit silently disabled interaction on the 2,000 km planet.
      const double radial_distance = glm::length(
          glm::dvec3(test_pos) - block_world_.planet().center);
      const double surface_band = block_world_.max_surface_height_above_base()
                                + static_cast<double>(k_pick_range) + 32.0;
      if (std::abs(radial_distance - block_world_.planet().radius) > surface_band) continue;
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
        expedition_mission_.on_block_removed(active_body_index_);
        record_block_edit(active_body_index_, addr, VoxelMaterial::Air, false);
        (void)save_persistent_game();
        // Remove stale mesh from scene so it will be rebuilt next frame.
        const uint64_t mid = BlockWorld::chunk_mesh_id(addr);
        const uint64_t vegetation_mid = vegetation::mesh_id(addr);
        scene.opaque_meshes.erase(
            std::remove_if(scene.opaque_meshes.begin(), scene.opaque_meshes.end(),
                           [mid, vegetation_mid](const RenderMesh &m) {
                             return m.mesh_id == mid ||
                                    m.mesh_id == vegetation_mid;
                           }),
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
            expedition_mission_.on_block_placed(active_body_index_);
            record_block_edit(active_body_index_, place_addr,
                              VoxelMaterial::Stone, true);
            (void)save_persistent_game();
            // Remove stale mesh for the affected chunk.
            const uint64_t mid = BlockWorld::chunk_mesh_id(place_addr);
            const uint64_t vegetation_mid = vegetation::mesh_id(place_addr);
            scene.opaque_meshes.erase(
                std::remove_if(scene.opaque_meshes.begin(), scene.opaque_meshes.end(),
                               [mid, vegetation_mid](const RenderMesh &m) {
                                 return m.mesh_id == mid ||
                                        m.mesh_id == vegetation_mid;
                               }),
                scene.opaque_meshes.end());
          }
        }
      }
    }
  }
  // ── Camera-relative rendering origin ─────────────────────────────────
  // Every retained mesh owns the double-precision origin used to build its
  // float vertices. The camera can therefore rebase every frame without
  // invalidating chunks or causing a visible streaming reset.
  {
    const glm::dvec3 cam_pos = glm::dvec3(camera.transform.position);
    camera_snap_origin_ = cam_pos;
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
    // AI debugging: log complete visual state each minute for AI agents.
    // Includes camera orientation (to reconstruct view), player state,
    // GPU workload, and LOD distribution — everything needed to understand
    // what the player sees without a screen.
    std::fprintf(stderr,
        "Frame %llu | Cam(%.0f,%.0f,%.0f) pitch=%d yaw=%d alt=%.0fm | "
        "Grounded=%s vel=%.0fm/s | "
        "FPS=%.0f dt=%.1fms | "
        "draw=%d verts=%d tris=%d | "
        "LOD: %d/%d/%d/%d chunks=%zu opaques=%zu | gen=%.1f mesh=%.1f upload=%.1fms\n",
        static_cast<unsigned long long>(frame_index),
        camera.transform.position.x, camera.transform.position.y, camera.transform.position.z,
        static_cast<int>(local_player.camera_rig.pitch),
        static_cast<int>(local_player.camera_rig.yaw),
        glm::length(local_player.transform.position) - block_world_.planet().radius,
        local_player.controller.grounded ? "yes" : "no",
        static_cast<double>(glm::length(local_player.controller.velocity)),
        render_stats.fps, render_stats.frame_ms,
        static_cast<int>(render_stats.draw_call_count),
        static_cast<int>(render_stats.total_vertices),
        static_cast<int>(render_stats.total_indices) / 3,
        render_stats.lod_chunk_count[0], render_stats.lod_chunk_count[1],
        render_stats.lod_chunk_count[2], render_stats.lod_chunk_count[3],
        block_world_.chunk_count(), scene.opaque_meshes.size(),
        render_stats.chunk_gen_ms, render_stats.mesh_build_ms,
        render_stats.gpu_upload_ms);

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
  const glm::dvec3 player_velocity(local_player.controller.velocity);
  const double stream_speed = glm::length(player_velocity);
  // A small planet brings the horizon into view quickly. Start the detailed
  // stream before the player reaches the old 32 m cutoff, with extra lead for
  // high-speed flight. This gives generation and meshing time to finish before
  // a chunk crosses the near-plane instead of relying on the coarse shell.
  const double stream_lead = std::clamp(
      16.0 + stream_speed * 0.75,
      16.0,
      block_world_.planet().radius * 1.5);
  const bool detailed_terrain_needed =
      camera_altitude <=
      kPlayablePlanetConfig.local_terrain_max_altitude_m + stream_lead;
  if (detailed_terrain_needed) {
    // Keep the recessed coarse globe behind the local voxel stream. Detailed
    // chunks own the visible silhouette when resident, while the fallback
    // prevents generation and eviction gaps from exposing the sky.
    const glm::dvec3 player_offset =
        glm::dvec3(local_player.transform.position) - block_world_.planet().center;
    const glm::dvec3 surface_direction = glm::dot(player_offset, player_offset) > 1.0e-9
                                             ? glm::normalize(player_offset)
                                             : glm::dvec3(1.0, 0.0, 0.0);
    const glm::dvec3 stream_anchor =
        block_world_.planet().center +
        surface_direction * block_world_.surface_radial_distance(surface_direction);
    const BlockAddress player_addr =
        block_world_.address_from_world(stream_anchor);
    const int32_t surface_shell = block_world_.shell_count() - 1;
    const int32_t chunk_radius = kSurfaceStreamRadiusChunks;

    // Hash the chunk center (incl. radial y) to detect player movement to a
    // new (x,z) column or crossing into a different radial chunk layer.
    uint64_t center_hash =
        (static_cast<uint64_t>(player_addr.chunk.x) << 32) ^
        (static_cast<uint64_t>(static_cast<uint32_t>(player_addr.chunk.z))) ^
        (static_cast<uint64_t>(static_cast<uint32_t>(player_addr.chunk.y)) << 16) ^
        (static_cast<uint64_t>(static_cast<uint8_t>(player_addr.sector)) << 48);
    const bool player_moved = (center_hash != last_chunk_center_hash_);

    // Predict ahead along the actual flight velocity. Detailed chunks are
    // requested around both the player and the future position, while a wider
    // guard band retains the old neighborhood until it is safely behind us.
    const double lookahead_seconds = std::clamp(
        0.35 + stream_speed /
                   std::max(64.0, block_world_.planet().radius * 4.0),
        0.35, 1.25);
    glm::dvec3 lookahead = player_velocity * lookahead_seconds;
    const double max_lookahead =
        std::min(192.0, block_world_.planet().radius * 1.5);
    const double lookahead_distance = glm::length(lookahead);
    if (lookahead_distance > max_lookahead) {
      lookahead *= max_lookahead / lookahead_distance;
    }
    const glm::dvec3 predicted_offset = player_offset + lookahead;
    const glm::dvec3 predicted_direction =
        glm::dot(predicted_offset, predicted_offset) > 1.0e-9
            ? glm::normalize(predicted_offset)
            : surface_direction;
    const glm::dvec3 predicted_anchor =
        block_world_.planet().center +
        predicted_direction *
            block_world_.surface_radial_distance(predicted_direction);
    const BlockAddress predicted_addr =
        block_world_.address_from_world(predicted_anchor);

    auto append_unique_chunks = [](
        std::vector<BlockAddress> &destination,
        const std::vector<BlockAddress> &source) {
      std::unordered_set<BlockAddress, BlockAddressHash> known(
          destination.begin(), destination.end());
      for (const BlockAddress &address : source) {
        if (known.insert(address).second) {
          destination.push_back(address);
        }
      }
    };

    // Collect desired chunks for the current and predicted surface positions,
    // including adjacent cube faces near sector edges.
    std::vector<BlockAddress> desired;
    block_world_.collect_stream_chunks(player_addr, surface_shell,
                                       chunk_radius, desired);
    std::vector<BlockAddress> predicted_desired;
    block_world_.collect_stream_chunks(predicted_addr, surface_shell,
                                       chunk_radius, predicted_desired);
    append_unique_chunks(desired, predicted_desired);

    std::vector<BlockAddress> resident;
    block_world_.collect_stream_chunks(player_addr, surface_shell,
                                       chunk_radius + 2, resident);
    std::vector<BlockAddress> predicted_resident;
    block_world_.collect_stream_chunks(predicted_addr, surface_shell,
                                       chunk_radius + 2,
                                       predicted_resident);
    append_unique_chunks(resident, predicted_resident);
    if (player_moved) {
      block_world_.evict_chunks_except(resident);
    }

    // Generate missing chunks nearest-first with a per-frame budget (Craft-style
    // streaming: show something quickly, fill the halo over subsequent frames).
    const PerfClock::time_point chunk_gen_start = PerfClock::now();
    auto chunk_dist_sq = [&](const BlockAddress &addr) -> double {
      const glm::dvec3 center =
          block_world_.world_from_address(chunk_address_key(addr));
      const glm::dvec3 current_delta = center - stream_anchor;
      const glm::dvec3 predicted_delta = center - predicted_anchor;
      return std::min(glm::dot(current_delta, current_delta) * 0.75,
                      glm::dot(predicted_delta, predicted_delta));
    };

    std::vector<BlockAddress> missing_chunks;
    missing_chunks.reserve(desired.size());
    for (const BlockAddress &addr : desired) {
      if (block_world_.find_chunk(addr) != nullptr) {
        continue;
      }
      missing_chunks.push_back(addr);
    }
    std::sort(missing_chunks.begin(), missing_chunks.end(),
              [&](const BlockAddress &a, const BlockAddress &b) {
                return chunk_dist_sq(a) < chunk_dist_sq(b);
              });

    std::vector<BlockAddress> new_chunks;
    new_chunks.reserve(missing_chunks.size());
    const uint32_t active_generation_budget = std::min<uint32_t>(
        12u, chunk_generation_budget_ +
                 static_cast<uint32_t>(stream_speed / 96.0));
    uint32_t gen_count = 0;
    for (const BlockAddress &addr : missing_chunks) {
      if (gen_count >= active_generation_budget) {
        break;
      }
      block_world_.get_or_generate_chunk(addr);
      new_chunks.push_back(addr);
      ++gen_count;
    }
    chunk_gen_ms = elapsed_ms(chunk_gen_start, PerfClock::now());

    // Full vertex rebuild only when the camera snap origin shifts (all stored
    // verts are offsets from snap). Player chunk changes are incremental:
    // evict far meshes and mesh the new halo — clearing everything on every
    // chunk step caused multi-second stalls and visible terrain holes.
    const bool need_full_rebuild = snap_origin_dirty_;
    // ── Frame profiler: time mesh building (face culling + greedy meshing) ──
    const PerfClock::time_point mesh_build_start = PerfClock::now();
    std::unordered_set<uint64_t> remesh_targets;
    if (!new_chunks.empty()) {
      collect_remesh_targets(block_world_, new_chunks, desired, remesh_targets);
    }

    auto mesh_in_scene = [&](uint64_t mid) -> bool {
      for (const RenderMesh &m : scene.opaque_meshes) {
        if (m.mesh_id == mid) {
          return true;
        }
      }
      return false;
    };

    bool has_pending_meshes = false;
    for (const BlockAddress &addr : desired) {
      BlockAddress ck = addr;
      ck.block = glm::ivec3(0);
      if (block_world_.find_chunk(ck) == nullptr) {
        continue;
      }
      if (!mesh_in_scene(BlockWorld::chunk_mesh_id(ck))) {
        has_pending_meshes = true;
        break;
      }
      if (!mesh_in_scene(vegetation::mesh_id(ck))) {
        has_pending_meshes = true;
        break;
      }
    }

    if (player_moved || !remesh_targets.empty() || snap_origin_dirty_ ||
        has_pending_meshes) {
      if (need_full_rebuild) {
        scene.opaque_meshes.clear();
        snap_origin_dirty_ = false;
      }
      if (player_moved) {
        last_chunk_center_hash_ = center_hash;
      }

      auto upsert_opaque_mesh = [&](RenderMesh &&mesh) {
        // Keep an empty mesh as a residency marker. Otherwise air-only radial
        // chunks look perpetually unmeshed and consume the mesh budget forever.
        for (RenderMesh &existing : scene.opaque_meshes) {
          if (existing.mesh_id == mesh.mesh_id) {
            existing = std::move(mesh);
            return;
          }
        }
        scene.opaque_meshes.push_back(std::move(mesh));
      };

      struct MeshWorkItem {
        BlockAddress chunk;
        double dist_sq = 0.0;
        ChunkLOD lod{};
      };
      std::vector<MeshWorkItem> mesh_work;
      mesh_work.reserve(desired.size());

      for (const BlockAddress &addr : desired) {
        BlockAddress ck = chunk_address_key(addr);
        const VoxelChunk *chunk = block_world_.find_chunk(ck);
        if (chunk == nullptr) {
          continue;
        }

        const glm::dvec3 chunk_center = block_world_.world_from_address(ck);
        const glm::dvec3 cam_pos = glm::dvec3(camera.transform.position);
        const float screen_h =
            std::max(1.0f, static_cast<float>(surface.height));
        const double chunk_ws =
            static_cast<double>(block_world_.config().chunk_size) *
            block_world_.config().block_size;
        const ChunkLOD clod = lod_system_.compute_lod(
            chunk_center, cam_pos, screen_h, chunk_ws);

        const uint64_t mid = BlockWorld::chunk_mesh_id(ck);
        bool is_new = false;
        for (const BlockAddress &nc : new_chunks) {
          if (chunk_address_key(nc) == ck) {
            is_new = true;
            break;
          }
        }

        const bool must_remesh =
            need_full_rebuild || is_new || !mesh_in_scene(mid) ||
            !mesh_in_scene(vegetation::mesh_id(ck)) ||
            remesh_targets.find(mid) != remesh_targets.end();
        if (!must_remesh) {
          continue;
        }

        MeshWorkItem item{};
        item.chunk = ck;
        item.dist_sq = chunk_dist_sq(ck);
        // Surface row at the player layer fills in first (visible shell top).
        if (ck.chunk.y == player_addr.chunk.y) {
          item.dist_sq *= 0.25;
        } else if (std::abs(ck.chunk.y - player_addr.chunk.y) == 1) {
          item.dist_sq *= 0.5;
        }
        item.lod = clod;
        mesh_work.push_back(item);
      }

      std::sort(mesh_work.begin(), mesh_work.end(),
                [](const MeshWorkItem &a, const MeshWorkItem &b) {
                  return a.dist_sq < b.dist_sq;
                });

      std::unordered_set<uint64_t> resident_ids;
      for (const BlockAddress &addr : resident) {
        const BlockAddress ck = chunk_address_key(addr);
        if (block_world_.find_chunk(ck) != nullptr) {
          resident_ids.insert(BlockWorld::chunk_mesh_id(ck));
          resident_ids.insert(vegetation::mesh_id(ck));
        }
      }
      uint32_t lod_distribution[4] = {0, 0, 0, 0};
      for (const BlockAddress &addr : desired) {
        BlockAddress ck = chunk_address_key(addr);
        if (block_world_.find_chunk(ck) == nullptr) {
          continue;
        }

        const glm::dvec3 chunk_center = block_world_.world_from_address(ck);
        const glm::dvec3 cam_pos = glm::dvec3(camera.transform.position);
        const float screen_h =
            std::max(1.0f, static_cast<float>(surface.height));
        const double chunk_ws =
            static_cast<double>(block_world_.config().chunk_size) *
            block_world_.config().block_size;
        const ChunkLOD clod = lod_system_.compute_lod(
            chunk_center, cam_pos, screen_h, chunk_ws);
        if (clod.level >= 0 && clod.level <= 3) {
          lod_distribution[clod.level]++;
        }
      }

      // Spread first-load mesh work across frames — meshing 90+ chunks on frame 0
      // blocked the main loop for 10+ seconds and stalled screenshot capture.
      // A hard per-frame budget is more important than filling the horizon in
      // one frame: startup spikes above 16.6 ms make a nominal 60 FPS game
      // feel broken, especially on tile-based mobile GPUs.
      const uint32_t active_mesh_budget = std::min<uint32_t>(
          10u, mesh_build_budget_ +
                   static_cast<uint32_t>(stream_speed / std::max(
                       1.0f,
                       kPlayablePlanetConfig.debug_flight_sprint_base_mps)));
      uint32_t mesh_count = 0;
      for (const MeshWorkItem &item : mesh_work) {
        if (mesh_count >= active_mesh_budget) {
          break;
        }

        const BlockAddress &ck = item.chunk;
        const VoxelChunk *chunk = block_world_.find_chunk(ck);
        if (chunk == nullptr) {
          continue;
        }

        auto solid_at = [this, &ck, chunk](const BlockAddress &na) -> bool {
          BlockAddress nk = na;
          nk.block = glm::ivec3(0);
          const VoxelChunk *nc =
              (nk == ck) ? chunk : block_world_.find_chunk(nk);
          if (nc == nullptr) {
            // Neighbor chunks are streamed independently, but terrain height
            // is deterministic. Sample the missing column directly so a mesh
            // can cap a stream boundary with the same voxel profile instead
            // of either emitting a false wall or leaving a visible slit.
            const int32_t gx =
                nk.chunk.x * block_world_.config().chunk_size + na.block.x;
            const int32_t gz =
                nk.chunk.z * block_world_.config().chunk_size + na.block.z;
            const int32_t layer =
                nk.chunk.y * block_world_.config().chunk_size + na.block.y;
            const int32_t surface_height =
                block_world_.terrain_height_at_face_uv(nk.sector, gx, gz);
            return layer <= surface_height;
          }
          return nc->solid(na.block.x, na.block.y, na.block.z);
        };

        RenderMesh mesh = block_world_.build_chunk_mesh(
            ck, *chunk, solid_at, camera_snap_origin_, item.lod.level);
        upsert_opaque_mesh(std::move(mesh));
        RenderMesh grass = vegetation::build_grass_mesh(
            block_world_, ck, *chunk, camera_snap_origin_, item.lod.level);
        upsert_opaque_mesh(std::move(grass));
        ++mesh_count;
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
                           [&resident_ids](const RenderMesh &m) {
                             if (m.mesh_id == 0 ||
                                 m.mesh_id == kGlobalPlanetSurfaceMeshId) {
                               return false;
                             }
                             return resident_ids.find(m.mesh_id) ==
                                    resident_ids.end();
                           }),
            meshes.end());
      }
    }
    mesh_build_ms = elapsed_ms(mesh_build_start, PerfClock::now());
  } else {
    // The complete global surface owns the view at altitude. Remove only
    // streamed chunk meshes; clearing the whole opaque scene caused a visible
    // one-frame hole and discarded unrelated stable meshes.
    block_world_.evict_chunks_except({});
    scene.opaque_meshes.erase(
        std::remove_if(scene.opaque_meshes.begin(), scene.opaque_meshes.end(),
                       [](const RenderMesh &mesh) {
                         return mesh.mesh_id != 0 &&
                                mesh.mesh_id != kGlobalPlanetSurfaceMeshId;
                       }),
        scene.opaque_meshes.end());
    for (int i = 0; i < 4; ++i) {
      render_stats.lod_chunk_count[i] = 0;
    }
    snap_origin_dirty_ = false;
    last_chunk_center_hash_ = 0;
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
  if (kPlayablePlanetConfig.atmosphere_preview_enabled) {
    scene.wireframe_meshes.push_back(atmosphere_wireframe_mesh_);
  }

  // ── Solar system update ──────────────────────────────────────────────
  // Advance orbital simulation with frame time and build celestial body
  // meshes BEFORE upload_scene so they are uploaded to GPU this frame.
  // WHY before upload: celestial meshes must reach the GPU for the
  // current frame's render; after would cause a 1-frame lag.
  solar_system_time_ += frame_dt;
  solar_system_.update(solar_system_time_);

  if (sky_navigation_mode_) {
    RenderMesh navigation_mesh{};
    const glm::dvec3 planet_orbit_pos =
        solar_system_.body_position(active_body_index_);
    const glm::dvec3 ring_right = glm::dvec3(camera.right());
    const glm::dvec3 ring_up = glm::dvec3(camera.up());
    const glm::dvec3 player_world =
        glm::dvec3(local_player.transform.position);

    for (int32_t i = 0; i < solar_system_.body_count(); ++i) {
      if (!is_sky_navigation_target(i)) continue;
      const CelestialBody &body = solar_system_.bodies()[static_cast<size_t>(i)];
      const glm::dvec3 body_world = body.position - planet_orbit_pos;
      const double distance = glm::length(body_world - player_world);
      const double ring_radius = std::clamp(
          std::max(6.0, body.orbital.radius * 1.4),
          6.0, std::max(6.0, distance * 0.02));
      const bool selected = i == sky_navigation_target_index_;
      const glm::vec3 ring_color = sky_navigation_locked_ && selected
          ? glm::vec3(1.0f, 0.82f, 0.24f)
          : selected ? glm::vec3(0.35f, 1.0f, 0.88f)
                     : glm::vec3(0.55f, 0.78f, 0.92f);
      append_dashed_ring(
          navigation_mesh, body_world, ring_right, ring_up, ring_radius,
          solar_system_time_ * (selected ? 1.2 : 0.7) + i * 0.8,
          ring_color);
    }

    if (sky_navigation_locked_ &&
        sky_navigation_target_index_ >= 0 &&
        sky_navigation_target_index_ < solar_system_.body_count()) {
      const CelestialBody &target = solar_system_.bodies()[static_cast<size_t>(
          sky_navigation_target_index_)];
      const glm::dvec3 target_world = target.position - planet_orbit_pos;
      append_dashed_line(navigation_mesh, player_world, target_world,
                         glm::vec3(1.0f, 0.82f, 0.24f), 48);
    }

    navigation_mesh.mesh_id = 0;
    navigation_mesh.content_hash = frame_index + 1;
    scene.wireframe_meshes.push_back(std::move(navigation_mesh));
  }

  // ── Coordinate frame update ─────────────────────────────────────────
  // Sync frame transforms from solar system orbital positions.
  // Then determine active frame based on vehicle/player altitude.
  frame_manager_.update(solar_system_);

  // Detect active coordinate frame based on altitude.
  // Uses the flight vehicle position when spawned and altitude > 0,
  // otherwise uses player position.
  {
    frame_transition_cooldown_ =
        std::max(0.0, frame_transition_cooldown_ - frame_dt);

    // Determine altitude: use vehicle if in flight, else player position.
    // Planet radius is the block-world sphere radius.
    const double planet_radius = block_world_.planet().radius;
    double altitude = 0.0;
    glm::dvec3 entity_pos = glm::dvec3(0.0);
    const bool vehicle_active = flight_vehicle_spawned_ &&
        flight_vehicle_.state().engine_active;
    if (vehicle_active) {
      const auto& vs = flight_vehicle_.state();
      entity_pos = vs.position;
      altitude = glm::length(vs.position) - planet_radius;
    } else {
      entity_pos = glm::dvec3(local_player.transform.position);
      altitude = glm::length(entity_pos) - planet_radius;
    }

    // Atmosphere height from atmosphere params.
    const double atm_height =
        static_cast<double>(atmosphere_.params().atmosphere_height);

    // Compute desired frame.
    const CoordinateFrame desired_frame =
        frame_manager_.current_frame(altitude, atm_height);

    // Apply hysteresis: require cooldown to expire before frame transition.
    if (desired_frame != active_frame_ &&
        frame_transition_cooldown_ <= 0.0) {
      active_frame_ = desired_frame;
      frame_transition_cooldown_ = k_frame_transition_hysteresis;

      // Log frame transition for debugging.
      switch (active_frame_) {
        case CoordinateFrame::Planet:
          active_frame_label_ = "Planet";
          spdlog::info("Frame transition: Planet (altitude={:.1f}m, atm={:.1f}m)",
                       altitude, atm_height);
          break;
        case CoordinateFrame::Orbital:
          active_frame_label_ = "Orbital";
          spdlog::info("Frame transition: Orbital (altitude={:.1f}m, atm={:.1f}m)",
                       altitude, atm_height);
          break;
        case CoordinateFrame::Solar:
          active_frame_label_ = "Solar";
          spdlog::info("Frame transition: Solar (altitude={:.1f}m, atm={:.1f}m)",
                       altitude, atm_height);
          break;
        default:
          active_frame_label_ = "Local";
          break;
      }
    }

    // ── SOI detection for interplanetary travel ───────────────────────
    // Check if the vehicle/player is in another body's SOI.
    // Convert vehicle position to Solar (heliocentric) frame for SOI check.
    const glm::dvec3 solar_pos = frame_manager_.transform(
        entity_pos, active_frame_, CoordinateFrame::Solar, active_body_index_);
    const int32_t soi_body = frame_manager_.detect_soi(solar_pos, solar_system_);

    // If SOI detection finds a different body and cooldown expired, switch.
    if (soi_body >= 0 && soi_body != active_body_index_ &&
        frame_transition_cooldown_ <= 0.0) {
      const auto& body = solar_system_.bodies()[static_cast<size_t>(soi_body)];
      // Entering a body's SOI is not by itself a landing. Keep the current
      // body-local terrain frame until the explicit approach solver has a
      // resident runtime and can atomically swap position, collision, and
      // streamed chunks together.
      if (soi_body != 3 || active_body_index_ != 1) {
        spdlog::info("SOI transition: entering {} SOI (body_index={})",
                     body.name, soi_body);
        active_body_index_ = soi_body;
        frame_transition_cooldown_ = k_frame_transition_hysteresis;
        // When entering a body's SOI, switch to its Orbital/Planet frame.
        if (altitude < atm_height) {
          active_frame_ = CoordinateFrame::Planet;
          active_frame_label_ = "Planet";
        } else {
          active_frame_ = CoordinateFrame::Orbital;
          active_frame_label_ = "Orbital";
        }
      }
    }


  }

  // Update sun direction from solar system into atmosphere.
  // WHY: the planet orbits the sun (planet at calculated position,
  // voxel world centred at origin).  We offset the solar-system
  // positions so the planet-centre stays at world (0,0,0) for
  // block-world compatibility, while the sun appears to move.
  {
    const glm::dvec3 planet_orbit_pos =
        solar_system_.body_position(active_body_index_);
    const glm::dvec3 cam_world = glm::dvec3(camera.transform.position);
    const glm::dvec3 sun_planet_centric = solar_system_.sun_position() - planet_orbit_pos;
    const glm::dvec3 sun_dir = glm::normalize(sun_planet_centric - cam_world);
    atmosphere_.set_sun_direction(sun_dir);
  }

  // ── Celestial body rendering ────────────────────────────────────────
  // Strip previous frame's celestial meshes from the end of opaque_meshes,
  // then append new camera-relative sphere meshes for the current frame.
  {
    // Celestial meshes are transient (mesh_id == 0). Remove by ownership,
    // not vector position: streamed chunks may have been appended after them.
    // Positional removal deleted fresh terrain and leaked old spheres.
    scene.opaque_meshes.erase(
        std::remove_if(scene.opaque_meshes.begin(), scene.opaque_meshes.end(),
                       [](const RenderMesh &mesh) { return mesh.mesh_id == 0; }),
        scene.opaque_meshes.end());
    last_celestial_mesh_count_ = 0;

    // Keep the complete terrain globe resident at every altitude. Its radial
    // bias places it behind the detailed voxel surface, and it closes holes
    // while chunks are being generated or evicted.
    const auto planet_it = std::find_if(
        scene.opaque_meshes.begin(), scene.opaque_meshes.end(),
        [](const RenderMesh &mesh) {
          return mesh.mesh_id == kGlobalPlanetSurfaceMeshId;
        });
    if (planet_it == scene.opaque_meshes.end()) {
      scene.opaque_meshes.push_back(global_planet_surface_mesh_);
    }

    const glm::dvec3 planet_orbit_pos =
        solar_system_.body_position(active_body_index_);

    for (int32_t i = 0; i < solar_system_.body_count(); ++i) {
      const CelestialBody& body = solar_system_.bodies()[i];
      // Skip the active body — its detailed terrain runtime owns the local
      // surface silhouette after landing.
      if (i == active_body_index_) continue;

      const glm::dvec3 body_world = body.position - planet_orbit_pos;
      const glm::vec3 rel_pos = camera_relative_position(
          body_world, scene.camera_origin);

      const float radius = static_cast<float>(body.orbital.radius);
      RenderMesh sphere = build_debug_sphere_mesh(rel_pos, radius, body.color);
      sphere.mesh_id = 0; // transient — always re-upload
      sphere.world_origin = scene.camera_origin.world_origin;
      scene.opaque_meshes.push_back(std::move(sphere));
      ++last_celestial_mesh_count_;
    }

    // Replicated clients use the same camera-relative coordinate path as
    // planetary terrain, preserving precision at two-million-metre radii.
    for (const auto &[player_id, state] : net_client_.player_states()) {
      if (player_id == 0 || player_id == net_client_.local_player_id()) {
        continue;
      }
      const glm::dvec3 player_world(state.x, state.y, state.z);
      const glm::vec3 player_relative =
          camera_relative_position(player_world, scene.camera_origin);
      RenderMesh player_mesh = build_debug_sphere_mesh(
          player_relative, 1.0f, player_color_from_network_id(player_id));
      player_mesh.mesh_id = 0;
      player_mesh.world_origin = scene.camera_origin.world_origin;
      scene.opaque_meshes.push_back(std::move(player_mesh));
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
    const float inside_atmosphere = static_cast<float>(std::clamp(
        1.0 - camera_altitude / atmosphere_.params().atmosphere_height,
        0.0, 1.0));
    // HDR daylight target; Reinhard tone mapping later produces the saturated
    // blue sky and strong terrain silhouette seen in the Meese reference.
    const glm::vec3 daylight_sky(0.80f, 1.55f, 7.0f);
    glm::vec3 physical_sky = atmosphere_state_.sky_color;
    for (int component = 0; component < 3; ++component) {
      if (!std::isfinite(physical_sky[component]) ||
          physical_sky[component] < 0.0f) {
        physical_sky[component] = 0.0f;
      }
    }
    physical_sky = glm::min(physical_sky, glm::vec3(4.0f));
    atmosphere_state_.sky_color = glm::mix(
        physical_sky, daylight_sky,
        0.72f * inside_atmosphere);
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
  ctx.grayscale_view = sky_navigation_mode_;

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
    if (sky_navigation_mode_) {
      const float luma = glm::dot(
          tm, glm::vec3(0.2126f, 0.7152f, 0.0722f));
      tm = glm::vec3(luma);
    }
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

void Engine::sync_network_state(uint32_t sim_tick,
                                const InputState &input) {
  if (!net_client_.is_connected()) {
    return;
  }

  NetTickInput tick_input{};
  tick_input.tick = sim_tick;
  tick_input.body_id = static_cast<uint32_t>(active_body_index_);
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
  local_player.camera_rig.yaw = 180.0f;
  local_player.camera_rig.pitch = -10.0f;
  local_player.camera_rig.distance = 0.0f;
}

GuiMenu::Character Engine::preferred_character() const {
  return session_state_.selected_character;
}

void Engine::rebuild_global_planet_surface() {
  global_planet_surface_mesh_ = build_compact_planet_surface_mesh(
      block_world_, kPlayablePlanetConfig.global_surface_subdivisions,
      kPlayablePlanetConfig.global_surface_radial_bias_m);
  global_planet_surface_mesh_.mesh_id = kGlobalPlanetSurfaceMeshId;
  // This is a recessed safety shell for the brief interval while a detailed
  // chunk is being generated. Render both faces so oblique approach rays can
  // never look through a missing chunk into the sky.
  global_planet_surface_mesh_.double_sided = true;
}

void Engine::switch_active_planet(int32_t body_index) {
  if (body_index == active_body_index_) {
    return;
  }
  if (!((active_body_index_ == 1 && body_index == 3) ||
        (active_body_index_ == 3 && body_index == 1))) {
    last_hud_message_ = "Terrain runtime unavailable for destination";
    return;
  }

  // Swap complete body-local runtimes, including resident chunks. This keeps
  // edits and collision data alive on both planets without pretending that a
  // single global voxel cache can represent two different surfaces.
  std::swap(block_world_, aster_block_world_);
  std::swap(collision_world, aster_collision_world);

  collision_world.set_planet_surface_collider(
      glm::vec3(0.0f), static_cast<float>(block_world_.planet().radius),
      static_cast<float>(block_world_.max_surface_height_above_base()),
      [this](glm::vec3 direction) -> float {
        return static_cast<float>(
            block_world_.surface_height_above_base(glm::dvec3(direction)));
      });
  aster_collision_world.set_planet_surface_collider(
      glm::vec3(0.0f), static_cast<float>(aster_block_world_.planet().radius),
      static_cast<float>(aster_block_world_.max_surface_height_above_base()),
      [this](glm::vec3 direction) -> float {
        return static_cast<float>(aster_block_world_.surface_height_above_base(
            glm::dvec3(direction)));
      });

  active_body_index_ = body_index;
  if (net_client_.is_connected()) {
    NetChunkInterest interest{};
    interest.body_id = static_cast<uint32_t>(body_index);
    interest.radius = 2;
    net_client_.set_chunk_interest(interest);
  }
  active_frame_ = CoordinateFrame::Planet;
  active_frame_label_ = "Planet";
  frame_transition_cooldown_ = k_frame_transition_hysteresis;

  wireframe_planet_ = block_world_.planet();
  wireframe_planet_dirty_ = true;
  atmosphere_wireframe_mesh_ = build_atmosphere_wireframe_mesh(
      wireframe_planet_, kPlayablePlanetConfig.atmosphere_height_m);
  rebuild_global_planet_surface();

  AtmosphereParams atmosphere_params = atmosphere_.params();
  atmosphere_params.planet_radius = block_world_.planet().radius;
  atmosphere_.init(atmosphere_params);
  last_chunk_center_hash_ = 0;
  snap_origin_dirty_ = true;
}

BlockWorld *Engine::world_for_body(int32_t body_index) {
  if (body_index == active_body_index_) {
    return &block_world_;
  }
  if ((active_body_index_ == 1 && body_index == 3) ||
      (active_body_index_ == 3 && body_index == 1)) {
    return &aster_block_world_;
  }
  return nullptr;
}

void Engine::record_block_edit(int32_t body_index,
                               const BlockAddress &address,
                               VoxelMaterial material, bool solid) {
  const auto existing = std::find_if(
      persistent_block_edits_.begin(), persistent_block_edits_.end(),
      [&](const PersistentBlockEdit &edit) {
        return edit.body_index == body_index && edit.address == address;
      });
  const PersistentBlockEdit replacement{
      .body_index = body_index,
      .address = address,
      .material = material,
      .solid = solid,
  };
  if (existing != persistent_block_edits_.end()) {
    *existing = replacement;
  } else {
    persistent_block_edits_.push_back(replacement);
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
    BlockWorld *world = world_for_body(edit.body_index);
    if (world == nullptr || edit.address.shell < 0 ||
        edit.address.shell >= world->shell_count()) {
      continue;
    }
    const int32_t chunk_size = world->config().chunk_size;
    const ShellConfig &shell = world->shell_config(edit.address.shell);
    const int32_t horizontal_chunks =
        (shell.horizontal_res + chunk_size - 1) / chunk_size;
    const int32_t vertical_chunks =
        (shell.vertical_layers + chunk_size - 1) / chunk_size;
    const bool valid_chunk = edit.address.chunk.x >= 0 &&
        edit.address.chunk.x < horizontal_chunks &&
        edit.address.chunk.z >= 0 &&
        edit.address.chunk.z < horizontal_chunks &&
        edit.address.chunk.y >= 0 &&
        edit.address.chunk.y < vertical_chunks;
    const bool valid_block = edit.address.block.x >= 0 &&
        edit.address.block.x < chunk_size && edit.address.block.y >= 0 &&
        edit.address.block.y < chunk_size && edit.address.block.z >= 0 &&
        edit.address.block.z < chunk_size;
    if (!valid_chunk || !valid_block) {
      continue;
    }
    VoxelChunk &chunk = world->get_or_generate_chunk(edit.address);
    chunk.set_material(edit.address.block.x, edit.address.block.y,
                       edit.address.block.z, edit.material);
    chunk.set_solid(edit.address.block.x, edit.address.block.y,
                    edit.address.block.z, edit.solid);
    persistent_block_edits_.push_back(edit);
  }

  (void)expedition_mission_.restore(state.expedition_stage);
  session_state_.selected_character = state.character;
  if (state.active_body_index != active_body_index_) {
    switch_active_planet(state.active_body_index);
  }
  local_player.transform.position = glm::vec3(state.player_local_position);
  local_player_prev_position = local_player.transform.position;
  camera_snap_origin_ = state.player_local_position;
  snap_origin_dirty_ = true;
  spdlog::info("Loaded save: body={}, edits={}, expedition={}",
               active_body_index_, persistent_block_edits_.size(),
               static_cast<int>(expedition_mission_.stage()));
}

bool Engine::save_persistent_game() {
  PersistentGameState state{};
  state.expedition_stage = expedition_mission_.stage();
  state.character = session_state_.selected_character;
  state.active_body_index = active_body_index_;
  state.player_local_position = glm::dvec3(local_player.transform.position);
  state.block_edits = persistent_block_edits_;
  std::string error;
  if (!save_persistent_game_state(platform_services, state, error)) {
    spdlog::warn("Could not save game state: {}", error);
    return false;
  }
  return true;
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

  // Use the same transported tangent frame as locomotion. It stays continuous
  // through the poles instead of switching reference axes abruptly.
  const glm::vec3 north =
      player_surface_orientation::reference_forward(player.camera_rig, local_up);
  const glm::vec3 east =
      player_surface_orientation::east_from_forward(north, local_up);

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
      append_screen_label(scene.debug_screen,
                          "Touch: RUN=DOWN  CROUCH=UP  JUMP=SELECT  MENU=CLOSE",
                          -0.88f, -0.64f, 0.0048f,
                          glm::vec3(0.88f, 0.95f, 1.0f));
    }
    return;
  }

  if (sky_navigation_mode_) {
    append_screen_rect(scene.debug_screen, -0.97f, 0.91f, -0.30f, 0.73f,
                       glm::vec3(0.03f, 0.05f, 0.08f));
    append_screen_label(scene.debug_screen, "SKY NAVIGATION", -0.93f, 0.86f,
                        0.0058f, glm::vec3(0.80f, 0.95f, 1.0f));
    append_screen_label(
        scene.debug_screen,
        sky_navigation_locked_
            ? "ENTER UNLOCK   F6 CLOSE"
            : "MOVE CURSOR TO SELECT   ENTER LOCK   F6 CLOSE",
        -0.93f, 0.78f, 0.0042f, glm::vec3(0.65f, 0.78f, 0.86f));
    append_screen_label(scene.debug_screen,
                        std::string(expedition_mission_.status()), -0.93f,
                        0.69f, 0.0043f, glm::vec3(0.92f, 0.95f, 1.0f));

    append_screen_cursor(scene.debug_screen, sky_navigation_cursor_ndc_,
                         glm::vec3(0.95f, 0.98f, 1.0f));

    const glm::mat4 vp = camera.projection(sky_navigation_aspect_ratio_) *
                         camera.view();
    const glm::dvec3 planet_orbit_pos =
        solar_system_.body_position(active_body_index_);
    const glm::dvec3 player_world =
        glm::dvec3(local_player.transform.position);
    for (int32_t i = 0; i < solar_system_.body_count(); ++i) {
      if (!is_sky_navigation_target(i)) continue;
      const CelestialBody &body = solar_system_.bodies()[static_cast<size_t>(i)];
      const glm::dvec3 body_world = body.position - planet_orbit_pos;
      glm::vec4 clip = vp * glm::vec4(glm::vec3(body_world), 1.0f);
      if (clip.w <= 0.0f) {
        clip.x = -clip.x;
        clip.y = -clip.y;
        clip.w = std::max(0.001f, -clip.w);
      }
      float x = clip.x / clip.w;
      float y = clip.y / clip.w;
      x = std::clamp(x, -0.88f, 0.88f);
      y = std::clamp(y, -0.78f, 0.78f);

      const bool selected = i == sky_navigation_target_index_;
      const glm::vec3 label_color = sky_navigation_locked_ && selected
          ? glm::vec3(1.0f, 0.84f, 0.28f)
          : selected ? glm::vec3(0.38f, 1.0f, 0.86f)
                     : glm::vec3(0.80f, 0.88f, 0.94f);
      const std::string name = uppercase_ascii(body.name);
      const double distance = glm::length(body_world - player_world);
      const std::string distance_text = distance >= 1000.0
          ? std::to_string(static_cast<int>(distance / 1000.0)) + "KM"
          : std::to_string(static_cast<int>(distance)) + "M";
      append_screen_label(scene.debug_screen,
                          (selected ? "> " : "  ") + name + " " + distance_text,
                          x - 0.06f, y, selected ? 0.0058f : 0.0048f,
                          label_color);
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

  append_screen_rect(scene.debug_screen, -0.94f, -0.63f, 0.24f, -0.88f,
                     expedition_mission_.complete()
                         ? glm::vec3(0.05f, 0.18f, 0.13f)
                         : glm::vec3(0.04f, 0.07f, 0.10f));
  append_screen_label(scene.debug_screen,
                      std::string(expedition_mission_.status()), -0.90f,
                      -0.69f, 0.0045f,
                      expedition_mission_.complete()
                          ? glm::vec3(0.45f, 1.0f, 0.68f)
                          : glm::vec3(0.92f, 0.96f, 1.0f));
  append_screen_label(scene.debug_screen,
                      std::string(expedition_mission_.hint()), -0.90f,
                      -0.78f, 0.0040f, glm::vec3(0.75f, 0.84f, 0.92f));

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
    const glm::dvec3 planet_pos =
        solar_system_.body_position(active_body_index_);
    overlay_text += "\nPlanet orbit pos: (" +
        std::to_string(static_cast<int>(planet_pos.x)) + ", " +
        std::to_string(static_cast<int>(planet_pos.y)) + ", " +
        std::to_string(static_cast<int>(planet_pos.z)) + ")";
  }
  overlay_text += "\nSolar time: " + std::to_string(static_cast<int>(solar_system_time_)) + "s";

  // ── Coordinate frame HUD info ──────────────────────────────────────
  overlay_text += "\nFrame: " + std::string(active_frame_label_);
  overlay_text += "  Body: ";
  {
    const auto& bodies = solar_system_.bodies();
    if (active_body_index_ >= 0 &&
        active_body_index_ < static_cast<int32_t>(bodies.size())) {
      overlay_text += bodies[static_cast<size_t>(active_body_index_)].name;
    } else {
      overlay_text += "?";
    }
  }
  {
    // Show altitude above current body surface.
    const double planet_radius = block_world_.planet().radius;
    double altitude = 0.0;
    if (flight_vehicle_spawned_ &&
        flight_vehicle_.state().engine_active) {
      altitude = glm::length(flight_vehicle_.state().position) - planet_radius;
    } else {
      altitude = glm::length(glm::dvec3(local_player.transform.position))
                 - planet_radius;
    }
    overlay_text += "\nAltitude: " + std::to_string(static_cast<int>(altitude)) + "m";
  }
  // Frame transition cooldown timer.
  if (frame_transition_cooldown_ > 0.0) {
    overlay_text += "  Cooldown: " +
        std::to_string(frame_transition_cooldown_).substr(0, 3) + "s";
  }

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
        glm::length(vs.position) - block_world_.planet().radius)) + "m";
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
