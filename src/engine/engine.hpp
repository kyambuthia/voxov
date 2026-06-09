#pragma once

#include "engine_gameplay/animation/animation_runtime.hpp"
#include "engine_gameplay/player/player_components.hpp"
#include "engine_gameplay/player/player_controller.hpp"
#include "engine_events/event_bus.hpp"
#include "engine_input/input_state.hpp"
#include "engine_math/camera.hpp"
#include "engine_net/lan_discovery.hpp"
#include "engine_physics/physics_solver.hpp"
#include "engine_physics/physics_world.hpp"
#include "engine_render/renderer.hpp"
#include "engine_runtime/runtime_game_session.hpp"
#include "engine_runtime/runtime_session_controller.hpp"
#include "engine_ui/gui_menu.hpp"
#include "engine_world/physics/voxel_collision.hpp"
#include "engine_world/planet.hpp"
#include "engine_world/planet_blocks.hpp"
#include "platform/platform_services.hpp"

#include <string>

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
  GuiMenu::Character selected_character = GuiMenu::Character::Capsule;
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

private:
  void update_third_person_camera(PlayerEntity &player, Camera &out_camera);
  void update_third_person_camera(PlayerEntity &player,
                                  const glm::vec3 &render_position,
                                  Camera &out_camera);
  void refresh_overlay_text();

  Renderer renderer;
  PlatformServices platform_services;
  RuntimeGameSession game_session;
  EventBus event_bus_;
  PhysicsWorld physics;
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

  RenderScene scene;

  // ── Block-based voxel planet (Bowerbyte architecture) ───────────────
  // 6 sectors → shells (doubling resolution) → 16³ chunks → blocks.
  // 3D noise on sphere for seamless terrain, gravity-aligned block
  // meshing with cross-face neighbor culling via cube net.
  BlockWorld block_world_;
  std::vector<BlockAddress> loaded_chunks_;    // currently resident chunks
  uint64_t block_mesh_revision_ = 0;
  uint32_t chunk_generation_budget_ = 4;
  uint64_t last_chunk_center_hash_ = 0;        // detect player movement

  bool debug_fly_mode_ = true; // start in fly mode (collision WIP)
  bool touch_controls_visible_ = false;
  RenderStats render_stats;
  EngineSessionState session_state_{};
  EngineRuntimeOptions runtime_options{};

  // ── Wireframe debug overlay ─────────────────────────────────────────
  // Same PlanetDefinition as terrain, coarser grid (64 cells/face = ~62 km/cell).
  // Rendered as colored lines per face (red=+X, blue=-X, green=+Y, etc.).
  // Generated once at init; GPU buffer cached by mesh_id in sokol renderer.
  PlanetDefinition wireframe_planet_{};
  RenderMesh wireframe_planet_mesh_{};
  bool wireframe_planet_dirty_ = true;

  uint64_t frame_index = 0;
  uint64_t presentation_frame_events_seen_ = 0;
  double last_frame_dt = 0.0;
  std::string last_hud_message_;
};
