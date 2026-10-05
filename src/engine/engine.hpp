#pragma once

#include "engine_gameplay/animation/animation_runtime.hpp"
#include "engine_gameplay/player/player_components.hpp"
#include "engine_gameplay/player/player_controller.hpp"
#include "engine_gameplay/objectives/expedition_mission.hpp"
#include "engine_events/event_bus.hpp"
#include "engine_input/input_state.hpp"
#include "engine_math/camera.hpp"
#include "engine_net/lan_discovery.hpp"
#include "engine_net/net_client.hpp"
#include "engine_net/net_server.hpp"
#if defined(VOXOV_PLATFORM_WEB)
#include "engine_net/web_net_client.hpp"
#endif
#include "engine_physics/flight_vehicle.hpp"
#include "engine_physics/physics_solver.hpp"
#include "engine_physics/physics_world.hpp"
#include "engine_render/renderer.hpp"
#include "engine_runtime/runtime_game_session.hpp"
#include "engine_runtime/persistent_game_state.hpp"
#include "engine_runtime/runtime_session_controller.hpp"
#include "engine_ui/gui_menu.hpp"
#include "engine_world/physics/voxel_collision.hpp"
#include "engine_world/planet.hpp"
#include "engine_world/planet_blocks.hpp"
#include "engine_world/planet_lod.hpp"
#include "engine_world/atmosphere.hpp"
#include "engine_world/coordinate_frames.hpp"
#include "engine_world/solar_system.hpp"
#include "platform/platform_services.hpp"

#include <string>
#include <optional>
#include <vector>

struct EngineRuntimeOptions {
  PhysicsSolverBackend physics_backend = PhysicsSolverBackend::AvbdExperimental;
  RenderBackendType render_backend = RenderBackendType::Sokol;
  // Borrowed only while Engine::init copies the platform configuration.
  const PlatformServices *platform_services = nullptr;
};

struct EngineInputFrame {
  InputState primary{};
  InputState secondary{};
  bool touch_mode = false;
};

struct EngineSessionState {
  bool gameplay_started = false;
  bool menu_open = true;
  bool devhud_enabled = false;
  GuiMenu::Character selected_character = GuiMenu::Character::Humanoid;
  GuiMenuView menu_view{};
};

enum class EngineConnectResult {
  Connected = 0,
  NetworkInitFailed = 1,
  ConnectFailed = 2,
};

class Engine {
public:
  bool init(const EngineRuntimeOptions &options);
  EngineConnectResult connect(const char *host, uint16_t port);
  void shutdown();
  void tick(double frame_dt, EngineInputFrame input_frame,
            const RenderSurface &surface);
  const RenderStats &stats() const;
  void set_session_state(const EngineSessionState &state);
  RuntimeSessionSnapshot session_snapshot() const;
  void pump_lan_discovery();
  bool pop_discovered_host(LanHostEntry &host);
  void abort_client_session();
  void host_local_session();
  void host_lan_session();
  void join_nearby_session();
  void leave_session();
  void reset_camera();
  GuiMenu::Character preferred_character() const;
  EventBus &events() { return event_bus_; }
  bool capture_screenshot(const char *filepath, int width, int height);

private:
  void update_first_person_camera(PlayerEntity &player, Camera &out_camera,
                                  float dt = 0.0f);
  void update_first_person_camera(PlayerEntity &player,
                                  const glm::vec3 &render_position,
                                  Camera &out_camera, float dt);
  void rebuild_global_planet_surface();
  void refresh_overlay_text();
  void switch_active_planet(int32_t body_index);
  BlockWorld *world_for_body(int32_t body_index);
  void record_block_edit(int32_t body_index, const BlockAddress &address,
                         VoxelMaterial material, bool solid);
  void load_persistent_game();
  bool save_persistent_game();
  // Marks the save dirty; flush_pending_save() writes it at most every
  // kSaveDebounceSeconds so block edits never hit the disk every click.
  void request_save();
  void flush_pending_save(double frame_dt);
  void update_block_interaction(const InputState &input);
  // Frame phases of tick(), in call order.
  void apply_session_input(InputState &gameplay_input,
                           const RenderSurface &surface);
  void apply_sky_navigation_assist(InputState &gameplay_input,
                                   double frame_dt);
  // Runs the fixed-step simulation callbacks and drains frame events.
  void simulate_frame(InputState &gameplay_input, EngineInputFrame &input_frame,
                      double frame_dt, ProfilingSnapshot &profiling_sample);
  struct TerrainStreamTimings {
    double chunk_gen_ms = 0.0;
    double mesh_build_ms = 0.0;
  };
  // Generates, meshes, and evicts surface chunks around the player.
  TerrainStreamTimings stream_block_terrain(double camera_altitude,
                                            const RenderSurface &surface);
  void rebuild_wireframe_overlays();
  // Advances orbits and builds the sky-navigation markers.
  void update_solar_system(double frame_dt);
  // Picks the active coordinate frame and handles SOI transitions.
  void update_coordinate_frames(double frame_dt);
  // Rebuilds transient sphere meshes for the non-active celestial bodies.
  void append_celestial_meshes();
  void update_render_stats(const ProfilingSnapshot &profiling_sample,
                           const TerrainStreamTimings &terrain_timings,
                           double gpu_upload_ms);
  // Computes the clear/sky colour for the current view.
  void update_sky_color(double camera_altitude);
  // Builds the per-frame render context; advances frame_index.
  RenderFrameContext build_render_context(double frame_dt,
                                          const RenderSurface &surface);
  // Updates view, clip planes, and FOV; returns the camera altitude above
  // the active planet's base radius.
  double update_camera(double frame_dt, const RenderSurface &surface);
  // Drops the chunk mesh holding `addr` plus any neighbour chunk sharing the
  // block's faces, so cross-chunk face culling is rebuilt next frame.
  void invalidate_block_meshes(const BlockAddress &addr);
  void sync_network_state(uint32_t sim_tick, const InputState &input);
  void start_local_server(uint16_t port, bool loopback_only);
  void stop_client_session();

  Renderer renderer;
  PlatformServices platform_services;
  RuntimeGameSession game_session;
  EventBus event_bus_;
#if defined(VOXOV_PLATFORM_WEB)
  WebNetClient net_client_;
#else
  NetClient net_client_;
#endif
  LanDiscovery lan_discovery_;
  NetServer local_server_;
  bool local_server_loopback_ = true;
  bool local_server_running_ = false;
  PhysicsWorld physics;
  FlightVehicle flight_vehicle_;
  bool flight_vehicle_spawned_ = false;
  uint64_t collision_count_ = 0;
  uint32_t net_events_seen_ = 0;
  std::string last_net_status_;
  NetClientConnectionState last_net_connection_state_ =
      NetClientConnectionState::Disconnected;

  VoxelCollisionWorld collision_world{nullptr};

  Camera camera;
  PlayerEntity local_player;
  glm::vec3 local_player_prev_position = glm::vec3(0.0f);
  PlayerAnimationRuntime local_player_animation;
  ExpeditionMission expedition_mission_;
  std::vector<PersistentBlockEdit> persistent_block_edits_;

  RenderScene scene;

  // ── Block-based voxel planet (Bowerbyte architecture) ───────────────
  // 6 sectors → shells (doubling resolution) → 16³ chunks → blocks.
  // 3D noise on sphere for seamless terrain, gravity-aligned block
  // meshing with cross-face neighbor culling via cube net.
  BlockWorld block_world_;
  // Aster is kept as a resident procedural runtime so a landing can swap
  // body-local terrain/collision state without destroying Voxov edits. This
  // becomes a cache of PlanetRuntime instances as the catalog grows.
  BlockWorld aster_block_world_;
  VoxelCollisionWorld aster_collision_world{nullptr};
  PlanetLODSystem lod_system_;
  std::vector<BlockAddress> loaded_chunks_;    // currently resident chunks
  uint64_t block_mesh_revision_ = 0;
  uint32_t chunk_generation_budget_ = 32;
  uint32_t mesh_build_budget_ = 24;
  uint64_t last_chunk_center_hash_ = 0;        // detect player movement

  bool debug_fly_mode_ = false; // false = surface walking (gravity toward planet center, capsule collision with voxel terrain)
  float third_person_distance_ = 4.5f; // saved orbit distance while in first-person
  bool third_person_camera_initialized_ = false;
  glm::vec3 third_person_pivot_{0.0f};
  float third_person_smoothed_distance_ = 4.5f;
  bool touch_controls_visible_ = false;
  RenderStats render_stats;
  EngineSessionState session_state_{};
  EngineRuntimeOptions runtime_options{};

  // ── Camera-relative rendering ────────────────────────────────────────
  // Snap origin tracks the camera-relative float32 reference point.
  // Mesh vertices are stored as offsets from this origin to preserve
  // float32 precision at 2000 km planet scale. The snap threshold grows with
  // altitude so fast flight does not constantly rebuild terrain meshes.
  glm::dvec3 camera_snap_origin_{0.0};
  bool snap_origin_dirty_ = true;       // force initial mesh build

  // Complete terrain-aware globe for compact-world flight and orbit. It is
  // static, cached, and built from the same height sampler as editable chunks.
  // A small radial inset makes it a safe underlay for streamed chunk seams.
  RenderMesh global_planet_surface_mesh_{};

  // ── Wireframe debug overlay ─────────────────────────────────────────
  // Same PlanetDefinition as terrain, coarser grid (64 cells/face = ~62 km/cell).
  // Rendered as colored lines per face (red=+X, blue=-X, green=+Y, etc.).
  // Generated once at init; GPU buffer cached by mesh_id in sokol renderer.
  PlanetDefinition wireframe_planet_{};
  RenderMesh wireframe_planet_mesh_{};
  RenderMesh atmosphere_wireframe_mesh_{};
  bool wireframe_planet_dirty_ = true;

  // ── Sky navigation mode ─────────────────────────────────────────────
  // F6 desaturates the world and exposes celestial destinations as a simple
  // selectable sky overlay. Enter locks the current target and draws a guide.
  bool sky_navigation_mode_ = false;
  bool sky_navigation_locked_ = false;
  int32_t sky_navigation_target_index_ = 3; // Aster in the starter system
  float sky_navigation_aspect_ratio_ = 16.0f / 9.0f;
  glm::vec2 sky_navigation_cursor_ndc_{0.0f};

  uint64_t frame_index = 0;
  uint64_t presentation_frame_events_seen_ = 0;
  double last_frame_dt = 0.0;
  std::string last_hud_message_;

    // ── Solar system ──────────────────────────────────────────────────
    // Manages Sun, Planet, Moon with Keplerian orbital mechanics.
    // Updated each frame with elapsed simulation time.
    SolarSystem solar_system_;
    double solar_system_time_ = 0.0;  // accumulated simulation time (seconds)
    size_t last_celestial_mesh_count_ = 0; // meshes appended to opaque_meshes

    // ── Coordinate frame manager ──────────────────────────────────────
    // Handles frame transitions: Planet ↔ Orbital ↔ Solar.
    // WHY: maintains float64 precision at solar scales by using
    // hierarchical coordinate frames with relative positions.
    CoordinateFrameManager frame_manager_;
    CoordinateFrame active_frame_ = CoordinateFrame::Planet;
    int32_t active_body_index_ = 1;  // 0=sun, 1=planet, 2=moon
    const char* active_frame_label_ = "Planet";
    // Hysteresis for frame transitions: prevent rapid toggling.
    double frame_transition_cooldown_ = 0.0;
    static constexpr double k_frame_transition_hysteresis = 2.0; // seconds

    // ── Atmosphere rendering (Rayleigh + Mie scattering) ───────────────
    // Computes sky color on CPU each frame; fragment shader applies
    // aerial perspective (transmittance) for terrain fragments.
    AtmosphereRenderer atmosphere_;
    AtmosphereState atmosphere_state_{};
    bool atmosphere_enabled_ = true;

    // ── Block interaction (first-person pick/break/place) ─────────────────
  // Targeted block from camera center raycast.
  size_t hotbar_slot_ = 0; // index into kBlockHotbar
  bool persistence_dirty_ = false;
  double seconds_since_save_ = 0.0;
  glm::dvec3 targeted_hit_pos_ = glm::dvec3(0.0);
  glm::vec3 targeted_face_normal_ = glm::vec3(0.0f);
  std::optional<BlockAddress> targeted_addr_;
};
