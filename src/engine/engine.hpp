#pragma once

#include "engine_events/event_bus.hpp"
#include "engine_gameplay/animation/animation_runtime.hpp"
#include "engine_gameplay/objectives/expedition_mission.hpp"
#include "engine_gameplay/player/player_components.hpp"
#include "engine_gameplay/player/player_controller.hpp"
#include "engine_gameplay/player/remote_avatar.hpp"
#include "engine_input/input_state.hpp"
#include "engine_math/camera.hpp"
#include "engine_net/lan_discovery.hpp"
#include "engine_net/net_client.hpp"
#include "engine_net/net_server.hpp"
#if defined(VOXOV_PLATFORM_WEB)
#include "engine_net/web_net_client.hpp"
#endif
#include "engine_physics/physics_solver.hpp"
#include "engine_physics/physics_world.hpp"
#include "engine_render/renderer.hpp"
#include "engine_runtime/persistent_game_state.hpp"
#include "engine_runtime/runtime_game_session.hpp"
#include "engine_runtime/runtime_session_controller.hpp"
#include "engine_ui/gui_menu.hpp"
#include "engine_world/flat_world.hpp"
#include "engine_world/physics/voxel_collision.hpp"
#include "platform/platform_services.hpp"

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
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
  struct TerrainStreamTimings {
    double chunk_gen_ms = 0.0;
    double mesh_build_ms = 0.0;
  };

  void update_first_person_camera(PlayerEntity &player, Camera &out_camera,
                                  float dt = 0.0f);
  void update_first_person_camera(PlayerEntity &player,
                                  const glm::vec3 &render_position,
                                  Camera &out_camera, float dt);
  void refresh_overlay_text();
  void install_active_surface_collider();
  void record_block_edit(const FlatBlockAddress &address,
                         VoxelMaterial material, bool solid);
  void apply_persistent_edits_to_chunk(VoxelChunkCoord2D coord);
  void load_persistent_game();
  bool save_persistent_game();
  void request_save();
  void flush_pending_save(double frame_dt);
  void update_block_interaction(const InputState &input);
  void append_target_outline(RenderMesh &mesh) const;
  void apply_session_input(InputState &gameplay_input);
  void simulate_frame(InputState &gameplay_input, EngineInputFrame &input_frame,
                      double frame_dt, ProfilingSnapshot &profiling_sample);
  TerrainStreamTimings stream_flat_terrain();
  void append_remote_player_meshes();
  void update_render_stats(const ProfilingSnapshot &profiling_sample,
                           const TerrainStreamTimings &terrain_timings,
                           double gpu_upload_ms);
  RenderFrameContext build_render_context(double frame_dt,
                                          const RenderSurface &surface);
  double update_camera(double frame_dt, const RenderSurface &surface);
  void invalidate_chunk_mesh(VoxelChunkCoord2D coord);
  void invalidate_block_meshes(const FlatBlockAddress &addr);
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
  uint64_t collision_count_ = 0;
  uint32_t net_events_seen_ = 0;
  std::string last_net_status_;
  NetClientConnectionState last_net_connection_state_ =
      NetClientConnectionState::Disconnected;

  VoxelCollisionWorld collision_world{nullptr};
  std::vector<VoxelCollisionChunk> active_collision_chunks_;
  VoxelChunkCoord2D last_collision_center_{0, 0};
  bool collision_chunks_dirty_ = true;

  Camera camera;
  PlayerEntity local_player;
  glm::vec3 local_player_prev_position = glm::vec3(0.0f);
  PlayerAnimationRuntime local_player_animation;
  ExpeditionMission expedition_mission_;
  std::vector<PersistentBlockEdit> persistent_block_edits_;

  RenderScene scene;

  FlatWorld flat_world_;
  std::unordered_set<VoxelChunkCoord2D, VoxelChunkCoord2DHash>
      dirty_chunk_meshes_;
  std::unordered_map<VoxelChunkCoord2D, uint64_t, VoxelChunkCoord2DHash>
      chunk_mesh_revisions_;
  uint64_t next_chunk_revision_ = 1;
  uint32_t chunk_generation_budget_ = 4;
  uint32_t mesh_build_budget_ = 4;

  bool debug_fly_mode_ = false;
  float third_person_distance_ = 4.5f;
  bool third_person_camera_initialized_ = false;
  glm::vec3 third_person_pivot_{0.0f};
  float third_person_smoothed_distance_ = 4.5f;
  bool touch_controls_visible_ = false;
  RenderStats render_stats;
  EngineSessionState session_state_{};
  EngineRuntimeOptions runtime_options{};

  glm::dvec3 camera_snap_origin_{0.0};
  float viewport_aspect_ratio_ = 16.0f / 9.0f;

  uint64_t frame_index = 0;
  uint64_t presentation_frame_events_seen_ = 0;
  double last_frame_dt = 0.0;
  std::string last_hud_message_;

  glm::vec3 sky_color_ = glm::vec3(0.45f, 0.72f, 0.96f);
  glm::vec3 sun_direction_ = glm::normalize(glm::vec3(0.45f, 0.80f, 0.35f));

  size_t hotbar_slot_ = 0;
  std::unordered_map<uint32_t, RemoteAvatarState> remote_avatars_;
  bool persistence_dirty_ = false;
  double seconds_since_save_ = 0.0;
  glm::vec3 targeted_face_normal_ = glm::vec3(0.0f);
  std::optional<FlatBlockAddress> targeted_addr_;
};
