#include "engine_core/cvar.hpp"
#include "engine_core/string_id.hpp"
#include "engine/planet_gameplay_config.hpp"
#include "engine_gameplay/minigames/minigames.hpp"
#include "engine_gameplay/objectives/expedition_mission.hpp"
#include "engine_gameplay/player/player_controller.hpp"
#include "engine_gameplay/player/surface_orientation.hpp"
#include "engine_math/camera.hpp"
#include "engine_net_proto/net_protocol_helpers.hpp"
#include "engine_runtime/runtime_game_session.hpp"
#include "engine_net_proto/net_types.hpp"
#include "engine_world/net_chunk_state.hpp"
#include "engine_world/planet.hpp"
#include "engine_world/planet_blocks.hpp"
#include "engine_world/solar_system.hpp"
#include "engine_world/coordinate_frames.hpp"
#include "engine_physics/avbd_solver.hpp"
#include "engine_physics/vehicle/aircraft_controller.hpp"
#include "engine_physics/vehicle/ground_vehicle_controller.hpp"
#include "engine_physics/vehicle/vehicle_damage_model.hpp"
#include "engine_physics/vehicle/vehicle_drivetrain.hpp"
#include "engine_physics/vehicle/vehicle_foundation.hpp"
#include "engine_physics/vehicle/vehicle_sandbox_scene.hpp"
#include "engine_physics/vehicle/voxel_vehicle_builder.hpp"
#include "engine_physics/voxel/voxel_physics_bridge.hpp"
#include "engine_world/physics/voxel_collision.hpp"
#include "engine_world/voxel_chunk.hpp"
#include "engine_world/vegetation.hpp"
#include "engine_world/world_gen.hpp"
#include "platform/android_platform.hpp"
#include "platform/web_platform.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

void test_expedition_mission_requires_ordered_player_actions() {
  ExpeditionMission mission;
  assert(mission.stage() == ExpeditionStage::CollectSample);
  assert(!mission.complete());
  assert(mission.progress() == 0.0f);

  mission.on_block_placed(1);
  mission.on_course_locked(3);
  assert(mission.stage() == ExpeditionStage::CollectSample);

  mission.on_block_removed(3);
  assert(mission.stage() == ExpeditionStage::CollectSample);
  mission.on_block_removed(1);
  assert(mission.stage() == ExpeditionStage::DeployBeacon);

  mission.on_block_placed(1);
  assert(mission.stage() == ExpeditionStage::PlotCourse);
  mission.on_course_locked(2);
  assert(mission.stage() == ExpeditionStage::PlotCourse);
  mission.on_course_locked(3);
  assert(mission.stage() == ExpeditionStage::ReachAster);

  mission.on_landed(1);
  assert(!mission.complete());
  mission.on_landed(3);
  assert(mission.complete());
  assert(mission.progress() == 1.0f);
}

void test_cvar_register_and_find() {
  CVAR_FLOAT(test_value, 42.0f, CvarFlags::None, "test cvar");
  Cvar* found = cvar_find("test_value"_sid);
  assert(found != nullptr);
  assert(found->value == 42.0f);
  assert(found->default_value == 42.0f);
}

void test_cvar_set_and_get_float() {
  CVAR_FLOAT(test_set, 10.0f, CvarFlags::None, "set test");
  cvar_set_float("test_set"_sid, 25.0f);
  assert(cvar_get_float("test_set"_sid) == 25.0f);
}

void test_cvar_readonly_blocked() {
  CVAR_FLOAT(test_readonly, 5.0f, CvarFlags::ReadOnly, "readonly test");
  cvar_set_float("test_readonly"_sid, 99.0f);
  assert(cvar_get_float("test_readonly"_sid) == 5.0f);
}

void test_cvar_bool() {
  CVAR_BOOL(test_bool, true, CvarFlags::None, "bool test");
  assert(cvar_get_bool("test_bool"_sid) == true);
}

void test_cvar_command_line_parse() {
  CVAR_BOOL(test_server, false, CvarFlags::None, "server mode");
  CVAR_FLOAT(test_port, 7777.0f, CvarFlags::None, "port number");
  const char* args[] = {"voxov", "--test-server", "--test-port", "9999"};
  cvar_parse_command_line(4, const_cast<char**>(args));
  assert(cvar_get_bool("test_server"_sid) == true);
  assert(cvar_get_int("test_port"_sid) == 9999);
}

void test_cvar_not_found_returns_zero() {
  assert(cvar_get_float("nonexistent"_sid) == 0.0f);
  assert(cvar_get_bool("nonexistent"_sid) == false);
}

void test_string_id_compile_time_hash() {
  constexpr auto id_walk = "walk"_sid;
  constexpr auto id_run = "run"_sid;
  static_assert(id_walk != 0, "hash must be non-zero");
  static_assert(id_walk == "walk"_sid, "same string must produce same hash");
  static_assert(id_walk != id_run, "different strings must produce different hashes");
}

void test_camera_vectors() {
  Camera camera;
  camera.transform.euler_radians = glm::vec3(0.0f, 0.0f, 0.0f);

  glm::vec3 f = camera.forward();
  glm::vec3 r = camera.right();

  assert(std::fabs(f.x) < 0.0001f);
  assert(std::fabs(f.y) < 0.0001f);
  assert(std::fabs(f.z + 1.0f) < 0.0001f);

  assert(std::fabs(r.x - 1.0f) < 0.0001f);
  assert(std::fabs(r.y) < 0.0001f);
  assert(std::fabs(r.z) < 0.0001f);
}

void test_camera_view_override_basis() {
  Camera camera;
  const glm::vec3 eye(0.0f, 2.0f, 5.0f);
  const glm::vec3 target(0.0f, 2.0f, 4.0f);
  const glm::vec3 up(0.0f, 1.0f, 0.0f);
  camera.set_view_override(glm::lookAt(eye, target, up));

  const glm::vec3 f = camera.forward();
  const glm::vec3 r = camera.right();
  const glm::vec3 u = camera.up();
  assert(std::fabs(f.x) < 0.0001f);
  assert(std::fabs(f.y) < 0.0001f);
  assert(std::fabs(f.z + 1.0f) < 0.0001f);
  assert(std::fabs(r.x - 1.0f) < 0.0001f);
  assert(std::fabs(u.y - 1.0f) < 0.0001f);

  camera.clear_view_override();
  assert(!camera.has_view_override());
}

void test_coordinate_frames_preserve_body_specific_origins() {
  SolarSystem solar_system;
  solar_system.init(64.0);
  solar_system.update(0.0);

  CoordinateFrameManager frames;
  frames.init();
  frames.update(solar_system);

  const glm::dvec3 local_point(3.0, 4.0, 5.0);
  const glm::dvec3 voxov_solar = frames.transform(
      local_point, CoordinateFrame::Planet, CoordinateFrame::Solar, 1);
  const glm::dvec3 aster_solar = frames.transform(
      local_point, CoordinateFrame::Planet, CoordinateFrame::Solar, 3);

  assert(glm::length(voxov_solar -
                     (local_point + solar_system.body_position(1))) < 1.0e-6);
  assert(glm::length(aster_solar -
                     (local_point + solar_system.body_position(3))) < 1.0e-6);
  assert(glm::length(aster_solar - voxov_solar) > 1.0);

  const glm::dvec3 round_trip = frames.transform(
      aster_solar, CoordinateFrame::Solar, CoordinateFrame::Planet, 3);
  assert(glm::length(round_trip - local_point) < 1.0e-6);
}

void test_surface_orientation_parallel_transports_through_poles() {
  CameraRig rig{};
  glm::vec3 previous = player_surface_orientation::reference_forward(
      rig, glm::vec3(1.0f, 0.0f, 0.0f));

  for (int degree = 1; degree <= 360; ++degree) {
    const float angle = glm::radians(static_cast<float>(degree));
    const glm::vec3 up(std::cos(angle), std::sin(angle), 0.0f);
    const glm::vec3 forward =
        player_surface_orientation::reference_forward(rig, up);
    const glm::vec3 east =
        player_surface_orientation::east_from_forward(forward, up);
    assert(std::fabs(glm::dot(forward, up)) < 1.0e-4f);
    assert(std::fabs(glm::length(forward) - 1.0f) < 1.0e-4f);
    assert(std::fabs(glm::dot(east, up)) < 1.0e-4f);
    assert(glm::dot(previous, forward) > 0.99f);
    previous = forward;
  }
}

void test_net_pod_serialization() {
  NetSnapshot in{};
  in.tick = 42;
  in.x = 1.5f;
  in.y = -2.0f;
  in.z = 9.25f;

  uint8_t buffer[sizeof(NetSnapshot)]{};
  bool write_ok = net_write_pod(buffer, sizeof(buffer), in);
  assert(write_ok);

  NetSnapshot out{};
  bool read_ok = net_read_pod(buffer, sizeof(buffer), out);
  assert(read_ok);

  assert(out.tick == in.tick);
  assert(std::fabs(out.x - in.x) < 0.0001f);
  assert(std::fabs(out.y - in.y) < 0.0001f);
  assert(std::fabs(out.z - in.z) < 0.0001f);
}

void test_session_info_serialization() {
  NetSessionInfo in{};
  net_copy_cstr(in.server_name, "VOXOV Host");
  in.world_seed = 0x12345678u;
  in.current_players = 3;
  in.max_players = 32;
  in.flags = net_session_flag(NetSessionFlags::LanAdvertised);

  uint8_t buffer[sizeof(NetSessionInfo)]{};
  assert(net_write_pod(buffer, sizeof(buffer), in));

  NetSessionInfo out{};
  assert(net_read_pod(buffer, sizeof(buffer), out));
  assert(std::strcmp(out.server_name, "VOXOV Host") == 0);
  assert(out.world_seed == in.world_seed);
  assert(out.current_players == in.current_players);
  assert(out.max_players == in.max_players);
  assert(net_session_flag_set(out.flags, NetSessionFlags::LanAdvertised));
}

void test_chunk_state_serialization() {
  NetChunkState in{};
  in.coord.x = 3;
  in.coord.z = -2;
  in.version = 17;
  in.world_seed = 0xD00DFEEDu;
  in.content_type = static_cast<uint8_t>(NetChunkContentType::ProceduralFlat);

  uint8_t buffer[sizeof(NetChunkState)]{};
  assert(net_write_pod(buffer, sizeof(buffer), in));

  NetChunkState out{};
  assert(net_read_pod(buffer, sizeof(buffer), out));
  assert(out.coord.x == in.coord.x);
  assert(out.coord.z == in.coord.z);
  assert(out.version == in.version);
  assert(out.world_seed == in.world_seed);
  assert(out.content_type == in.content_type);
}

void test_chunk_runtime_helpers() {
  NetChunkCoord origin{};
  const NetChunkState flat = net_make_flat_chunk_state(origin, 9, 1234u);
  const NetChunkState same = net_make_flat_chunk_state(origin, 9, 1234u);
  const NetChunkState different = net_make_flat_chunk_state(origin, 10, 1234u);

  assert(net_chunk_state_matches(flat, same));
  assert(!net_chunk_state_matches(flat, different));

  NetSessionInfo info{};
  net_copy_cstr(info.server_name, "LAN Session");
  info.current_players = 2;
  info.max_players = 8;
  info.flags = net_session_flag(NetSessionFlags::LanAdvertised);
  assert(net_session_status_line(info) == "LAN Session [2/8] WI-FI");
}

void test_planet_face_uv_to_direction_unit_vectors() {
  const PlanetFace faces[] = {PlanetFace::PosX, PlanetFace::NegX,
                              PlanetFace::PosY, PlanetFace::NegY,
                              PlanetFace::PosZ, PlanetFace::NegZ};
  for (const PlanetFace face : faces) {
    const glm::dvec3 direction = face_uv_to_direction(face, 0.25, -0.5);
    assert(std::fabs(glm::length(direction) - 1.0) < 1.0e-9);
  }
}

void test_planet_direction_to_face() {
  assert(direction_to_face(glm::dvec3(2.0, 0.5, 0.25)) == PlanetFace::PosX);
  assert(direction_to_face(glm::dvec3(-2.0, 0.5, 0.25)) == PlanetFace::NegX);
  assert(direction_to_face(glm::dvec3(0.25, 2.0, 0.5)) == PlanetFace::PosY);
  assert(direction_to_face(glm::dvec3(0.25, -2.0, 0.5)) == PlanetFace::NegY);
  assert(direction_to_face(glm::dvec3(0.25, 0.5, 2.0)) == PlanetFace::PosZ);
  assert(direction_to_face(glm::dvec3(0.25, 0.5, -2.0)) == PlanetFace::NegZ);
}

void test_planet_direction_to_face_uv_roundtrip() {
  const PlanetFace faces[] = {PlanetFace::PosX, PlanetFace::NegX,
                              PlanetFace::PosY, PlanetFace::NegY,
                              PlanetFace::PosZ, PlanetFace::NegZ};
  for (const PlanetFace face : faces) {
    const glm::dvec3 direction = face_uv_to_direction(face, -0.375, 0.625);
    const PlanetFaceUV uv = direction_to_face_uv(direction);
    assert(uv.face == face);
    assert(std::fabs(uv.u + 0.375) < 1.0e-9);
    assert(std::fabs(uv.v - 0.625) < 1.0e-9);
  }
}

void test_planet_radial_up_and_world_pos() {
  PlanetDefinition planet{};
  planet.center = glm::dvec3(10.0, -5.0, 2.0);
  planet.radius = 1000.0;

  const glm::dvec3 surface =
      voxel_world_pos(planet, PlanetFace::PosY, 0.0, 0.0, 12.5);
  assert(std::fabs(glm::distance(surface, planet.center) - 1012.5) < 1.0e-9);

  const glm::dvec3 up = radial_up(planet, surface);
  assert(std::fabs(glm::length(up) - 1.0) < 1.0e-9);
  assert(glm::dot(up, surface - planet.center) > 0.0);
}

void test_planet_local_face_world_roundtrip_and_distortion() {
  PlanetDefinition planet{};
  planet.center = glm::dvec3(3.0, -4.0, 7.0);
  planet.radius = 512.0;

  LocalFaceVoxelCoords local{};
  local.face = PlanetFace::PosZ;
  local.xyz = glm::dvec3(128.0, 42.0, -96.0);

  const glm::dvec3 world =
      local_face_voxel_to_world_sphere(planet, local);
  const LocalFaceVoxelCoords roundtrip =
      world_sphere_to_local_face_voxel(planet, world);

  assert(roundtrip.face == local.face);
  assert(std::fabs(roundtrip.xyz.x - local.xyz.x) < 1.0e-9);
  assert(std::fabs(roundtrip.xyz.y - local.xyz.y) < 1.0e-9);
  assert(std::fabs(roundtrip.xyz.z - local.xyz.z) < 1.0e-9);

  LocalFaceVoxelCoords center{};
  center.xyz = glm::dvec3(0.0);
  LocalFaceVoxelCoords corner{};
  corner.xyz = glm::dvec3(planet.radius, 0.0, planet.radius);
  assert(std::fabs(cubed_sphere_distortion_factor(center, planet.radius) -
                   1.0) < 1.0e-9);
  assert(std::fabs(cubed_sphere_distortion_factor(corner, planet.radius) -
                   1.6180339887498948) < 1.0e-9);
}

void test_planet_tangent_basis_orthonormal() {
  const PlanetTangentBasis basis = tangent_basis(glm::dvec3(0.0, 1.0, 0.0));
  assert(std::fabs(glm::length(basis.up) - 1.0) < 1.0e-9);
  assert(std::fabs(glm::length(basis.east) - 1.0) < 1.0e-9);
  assert(std::fabs(glm::length(basis.north) - 1.0) < 1.0e-9);
  assert(std::fabs(glm::dot(basis.east, basis.up)) < 1.0e-9);
  assert(std::fabs(glm::dot(basis.north, basis.up)) < 1.0e-9);
  assert(std::fabs(glm::dot(basis.east, basis.north)) < 1.0e-9);
}

void test_planet_neighbor_within_face_bounds() {
  const PlanetChunkId id{PlanetFace::PosZ, 4, 7, 1};
  const PlanetChunkId neighbor = neighbor_chunk_id(id, 2, -3, 16);
  assert(neighbor.face == PlanetFace::PosZ);
  assert(neighbor.x == 6);
  assert(neighbor.y == 4);
  assert(neighbor.lod == 1);

  const PlanetChunkId clamped = neighbor_chunk_id(id, -20, 30, 16);
  assert(clamped.x == 0);
  assert(clamped.y == 15);
}

void test_block_world_terrain_height_is_smooth_across_columns() {
  BlockWorldConfig cfg{};
  cfg.planet.radius = 50.0;
  cfg.planet.center = glm::dvec3(0.0);
  cfg.surface_shells = 4;
  cfg.block_size = 1.0;
  cfg.chunk_size = 16;
  cfg.seed = 42;
  BlockWorld world{};
  world.init(cfg);

  const int32_t shell = world.shell_count() - 1;
  const int32_t res = world.shell_config(shell).horizontal_res;
  int32_t max_step = 0;
  for (int32_t z = 1; z < res - 1; ++z) {
    for (int32_t x = 1; x < res - 1; ++x) {
      const int32_t h = world.terrain_height_at_face_uv(
          PlanetFace::PosX, x, z);
      const int32_t hx = world.terrain_height_at_face_uv(
          PlanetFace::PosX, x + 1, z);
      const int32_t hz = world.terrain_height_at_face_uv(
          PlanetFace::PosX, x, z + 1);
      max_step = std::max(max_step, std::abs(hx - h));
      max_step = std::max(max_step, std::abs(hz - h));
    }
  }
  // HF direction noise produced single-block spikes; smooth UV FBM stays gradual.
  // Pit fill allows one layer below neighbors, so steps can be 2 across a pit edge.
  assert(max_step <= 4);
}

void test_compact_planet_profile_has_playable_scale() {
  BlockWorldConfig cfg{};
  cfg.planet.radius = kPlayablePlanetConfig.radius_m;
  cfg.planet.voxel_size = kPlayablePlanetConfig.voxel_size_m;
  cfg.surface_shells = kPlayablePlanetConfig.surface_shells;
  cfg.base_resolution = kPlayablePlanetConfig.base_resolution;
  cfg.block_size = kPlayablePlanetConfig.voxel_size_m;
  cfg.terrain_feature_size = kPlayablePlanetConfig.terrain_feature_size_m;
  cfg.terrain_base_height =
      kPlayablePlanetConfig.terrain_base_height_blocks;
  cfg.terrain_amplitude = kPlayablePlanetConfig.terrain_amplitude_blocks;
  cfg.terrain_min_height = kPlayablePlanetConfig.terrain_min_height_blocks;
  cfg.terrain_max_height = kPlayablePlanetConfig.terrain_max_height_blocks;
  cfg.terrain_shell_margin =
      kPlayablePlanetConfig.terrain_shell_margin_blocks;
  cfg.chunk_size = kPlayablePlanetConfig.chunk_size;
  cfg.seed = 42;

  BlockWorld world{};
  world.init(cfg);
  assert(world.shell_count() == 2);
  const ShellConfig &surface = world.shell_config(world.shell_count() - 1);
  assert(surface.horizontal_res == 128);
  assert(surface.vertical_layers == 9);
  assert(std::fabs(surface.inner_radius - 64.0) < 1.0e-9);
  assert(std::fabs(surface.outer_radius - 73.0) < 1.0e-9);

  const glm::dvec3 directions[] = {
      {1.0, 0.0, 0.0}, {-1.0, 0.0, 0.0},
      {0.0, 1.0, 0.0}, {0.0, -1.0, 0.0},
      {0.0, 0.0, 1.0}, {0.0, 0.0, -1.0},
      glm::normalize(glm::dvec3(1.0, 1.0, 1.0)),
      glm::normalize(glm::dvec3(-1.0, 0.3, 0.7)),
  };
  for (const glm::dvec3 &direction : directions) {
    const int32_t height = world.terrain_height_at(direction);
    assert(height >= kPlayablePlanetConfig.terrain_min_height_blocks);
    assert(height <= kPlayablePlanetConfig.terrain_max_height_blocks);
  }

  assert(std::fabs(planet_flight_clipmap_half_extent(64.0, 0.0) - 16.0) <
         1.0e-9);
  assert(std::fabs(planet_flight_clipmap_half_extent(64.0, 128.0) - 16.0) <
         1.0e-9);
}

void test_block_world_production_radius_keeps_meter_scale_detail() {
  BlockWorldConfig cfg{};
  cfg.planet.radius = 2'000'000.0;
  cfg.block_size = 1.0;
  cfg.terrain_feature_size = 512.0;
  cfg.chunk_size = 16;
  cfg.seed = 42;
  BlockWorld world{};
  world.init(cfg);

  const int32_t shell = world.shell_count() - 1;
  const ShellConfig &surface = world.shell_config(shell);
  assert(surface.horizontal_res == 4'194'304);

  const int32_t center = surface.horizontal_res / 2;
  int32_t min_height = 1000;
  int32_t max_height = -1000;
  for (int32_t offset = -1024; offset <= 1024; offset += 32) {
    const int32_t height = world.terrain_height_at_face_uv(
        PlanetFace::PosX, center + offset, center + offset / 3);
    min_height = std::min(min_height, height);
    max_height = std::max(max_height, height);
  }
  assert(max_height - min_height >= 2);

  BlockAddress a{};
  a.sector = PlanetFace::PosX;
  a.shell = shell;
  a.chunk = glm::ivec3(center / cfg.chunk_size, 1,
                       center / cfg.chunk_size);
  a.block = glm::ivec3(center % cfg.chunk_size, 0,
                       center % cfg.chunk_size);
  BlockAddress b = a;
  ++b.block.x;
  const double spacing = glm::distance(world.world_from_address(a),
                                       world.world_from_address(b));
  // Power-of-two face resolution slightly oversamples the equiangular face
  // centre; the target is at most one metre, never a multi-metre voxel.
  assert(spacing > 0.7 && spacing <= 1.0);
}

void test_block_world_evicts_chunks_outside_resident_patch() {
  BlockWorldConfig cfg{};
  cfg.planet.radius = 50.0;
  cfg.chunk_size = 16;
  BlockWorld world{};
  world.init(cfg);

  BlockAddress first{};
  first.shell = world.shell_count() - 1;
  first.chunk = glm::ivec3(1, 0, 1);
  BlockAddress second = first;
  second.chunk.x = 2;
  world.get_or_generate_chunk(first);
  world.get_or_generate_chunk(second);
  assert(world.chunk_count() == 2);
  assert(world.evict_chunks_except({first}) == 1);
  assert(world.chunk_count() == 1);
  assert(world.find_chunk(first) != nullptr);
}

void test_block_world_stream_includes_all_vertical_rows() {
  BlockWorldConfig cfg{};
  cfg.planet.radius = 50.0;
  cfg.planet.center = glm::dvec3(0.0);
  cfg.surface_shells = 4;
  cfg.block_size = 1.0;
  cfg.chunk_size = 16;
  BlockWorld world{};
  world.init(cfg);

  BlockAddress origin{};
  origin.sector = PlanetFace::PosX;
  origin.shell = world.shell_count() - 1;
  origin.chunk = glm::ivec3(2, 1, 2);

  std::vector<BlockAddress> stream{};
  world.collect_stream_chunks(origin, origin.shell, 1, stream);
  for (size_t i = 0; i < stream.size(); ++i) {
    for (size_t j = i + 1; j < stream.size(); ++j) {
      assert(!(stream[i] == stream[j]));
    }
  }

  const int32_t vc =
      (world.shell_config(origin.shell).vertical_layers + cfg.chunk_size - 1) /
      cfg.chunk_size;
  int32_t rows_seen = 0;
  for (int32_t cy = 0; cy < vc; ++cy) {
    bool has_row = false;
    for (const BlockAddress &addr : stream) {
      if (addr.sector == origin.sector && addr.chunk.y == cy) {
        has_row = true;
        break;
      }
    }
    if (has_row) {
      ++rows_seen;
    }
  }
  assert(rows_seen == vc);
}

void test_block_world_streamed_surface_meshes_are_non_empty() {
  BlockWorldConfig cfg{};
  cfg.planet.radius = 50.0;
  cfg.planet.center = glm::dvec3(0.0);
  cfg.surface_shells = 8;
  cfg.block_size = 1.0;
  cfg.chunk_size = 16;
  cfg.seed = k_voxov_flat_world_seed;
  BlockWorld world{};
  world.init(cfg);

  const glm::dvec3 spawn_dir = glm::normalize(glm::dvec3(1.0, 0.0, 0.0));
  const BlockAddress player = world.address_from_world(
      glm::dvec3(spawn_dir * (cfg.planet.radius + 20.0)));
  const int32_t shell = world.shell_count() - 1;

  std::vector<BlockAddress> stream{};
  world.collect_stream_chunks(player, shell, 3, stream);

  for (const BlockAddress &addr : stream) {
    world.get_or_generate_chunk(addr);
  }

  size_t non_empty_meshes = 0;
  size_t surface_row_non_empty = 0;
  const int32_t vc =
      (world.shell_config(shell).vertical_layers + cfg.chunk_size - 1) /
      cfg.chunk_size;
  const int32_t top_row = vc - 1;

  for (const BlockAddress &addr : stream) {
    BlockAddress ck = addr;
    ck.block = glm::ivec3(0);
    const VoxelChunk *chunk = world.find_chunk(ck);
    assert(chunk != nullptr);

    auto solid_at = [&world, &ck, chunk](const BlockAddress &na) -> bool {
      BlockAddress nk = na;
      nk.block = glm::ivec3(0);
      const VoxelChunk *nc =
          (nk.sector == ck.sector && nk.shell == ck.shell && nk.chunk == ck.chunk)
              ? chunk
              : world.find_chunk(nk);
      if (nc == nullptr) {
        return false;
      }
      return nc->solid(na.block.x, na.block.y, na.block.z);
    };

    const RenderMesh mesh =
        world.build_chunk_mesh(ck, *chunk, solid_at, glm::dvec3(0.0), 0);
    if (!mesh.vertices.empty()) {
      ++non_empty_meshes;
      if (ck.chunk.y == top_row) ++surface_row_non_empty;
    }
  }

  // Radial rows above a low valley are legitimately empty; the streamed patch
  // only needs visible terrain overall and at least one upper-row surface.
  assert(non_empty_meshes > 0);
  assert(surface_row_non_empty > 0);
}

void test_player_spawn_on_planet_surface() {
  BlockWorldConfig cfg{};
  cfg.planet.radius = 50.0;
  cfg.planet.center = glm::dvec3(0.0);
  cfg.surface_shells = 8;
  cfg.block_size = 1.0;
  cfg.chunk_size = 16;
  cfg.seed = 42;
  BlockWorld world{};
  world.init(cfg);

  VoxelCollisionWorld collision{};
  collision.set_planet_surface_collider(
      glm::vec3(0.0f),
      static_cast<float>(cfg.planet.radius),
      static_cast<float>(world.max_surface_height_above_base()),
      [&world](glm::vec3 direction) -> float {
        return static_cast<float>(
            world.surface_height_above_base(glm::dvec3(direction)));
      });

  const int32_t shell = world.shell_count() - 1;
  const int32_t col = world.shell_config(shell).horizontal_res / 2;
  PlayerEntity player = PlayerControllerSystem::spawn_on_planet_surface(
      world, collision, PlanetFace::PosX, col, col, 2.0);
  player.controller.capsuleRadius = 0.7f;

  glm::vec3 surface_point(0.0f);
  glm::vec3 up(0.0f, 1.0f, 0.0f);
  assert(collision.planet_surface_point(
      player.transform.position, surface_point, up));
  const float height_above =
      glm::dot(player.transform.position - surface_point, up);
  assert(height_above > 0.5f);
  assert(height_above < 4.0f);
}

void test_player_moves_on_planet_surface() {
  BlockWorldConfig cfg{};
  cfg.planet.radius = 50.0;
  cfg.planet.center = glm::dvec3(0.0);
  cfg.surface_shells = 8;
  cfg.block_size = 1.0;
  cfg.chunk_size = 16;
  cfg.seed = 42;
  BlockWorld world{};
  world.init(cfg);

  VoxelCollisionWorld collision{};
  collision.set_planet_surface_collider(
      glm::vec3(0.0f),
      static_cast<float>(cfg.planet.radius),
      static_cast<float>(world.max_surface_height_above_base()),
      [&world](glm::vec3 direction) -> float {
        return static_cast<float>(
            world.surface_height_above_base(glm::dvec3(direction)));
      });

  const int32_t shell = world.shell_count() - 1;
  const int32_t col = world.shell_config(shell).horizontal_res / 2;
  PlayerEntity player = PlayerControllerSystem::spawn_on_planet_surface(
      world, collision, PlanetFace::PosX, col, col, 2.0);
  player.controller.capsuleRadius = 0.7f;
  player.controller.capsuleHeight = 1.8f;
  player.controller.grounded = true;

  InputState input{};
  input.move = glm::vec2(0.0f, 1.0f);

  const glm::vec3 start = player.transform.position;
  const glm::vec3 up = collision.planet_up_at(start);
  for (int i = 0; i < 90; ++i) {
    PlayerControllerSystem::simulate_fixed(
        player, input, collision, 1.0f / 60.0f, false);
  }

  const glm::vec3 delta = player.transform.position - start;
  const float tangential =
      glm::length(delta - up * glm::dot(delta, up));
  assert(tangential > 0.5f);
}

void test_planet_flight_follows_camera_pitch() {
  VoxelCollisionWorld collision{};
  collision.set_planet_surface_collider(glm::vec3(0.0f), 50.0f);

  PlayerEntity player{};
  player.transform.position = glm::vec3(60.0f, 0.0f, 0.0f);
  player.camera_rig.yaw = 180.0f;
  player.camera_rig.pitch = 60.0f;

  InputState input{};
  input.move = glm::vec2(0.0f, 1.0f);
  const glm::vec3 start = player.transform.position;
  const glm::vec3 up = collision.planet_up_at(start);
  PlayerControllerSystem::simulate_fixed(
      player, input, collision, 1.0f / 60.0f, true);

  const glm::vec3 delta = player.transform.position - start;
  assert(glm::dot(delta, up) > 0.5f);
  assert(glm::length(player.controller.velocity) > 1.0f);
}

void test_planet_flight_descends_and_lands_on_surface() {
  VoxelCollisionWorld collision{};
  collision.set_planet_surface_collider(glm::vec3(0.0f), 50.0f);

  PlayerEntity player{};
  player.transform.position = glm::vec3(55.0f, 0.0f, 0.0f);
  player.controller.capsuleRadius = 0.7f;
  player.controller.capsuleHeight = 1.8f;
  player.locomotion_tuning.flight_speed = 20.0f;
  player.locomotion_tuning.flight_acceleration = 120.0f;
  player.locomotion_tuning.flight_atmosphere_height = 32.0f;

  InputState input{};
  for (int i = 0; i < 180 && !player.controller.grounded; ++i) {
    PlayerControllerSystem::simulate_fixed(
        player, input, collision, 1.0f / 60.0f, true);
  }


  assert(player.controller.grounded);
  assert(player.flight.phase == PlayerFlightPhase::Grounded);
  assert(player.flight.altitude_m < 0.5f);
  assert(glm::dot(player.controller.velocity,
                  collision.planet_up_at(player.transform.position)) >= -0.01f);
}

void test_block_world_cross_sector_chunk_offset() {
  BlockWorldConfig cfg{};
  cfg.planet.radius = 50.0;
  cfg.planet.center = glm::dvec3(0.0);
  cfg.surface_shells = 4;
  cfg.block_size = 1.0;
  cfg.chunk_size = 16;
  BlockWorld world{};
  world.init(cfg);

  BlockAddress origin{};
  origin.sector = PlanetFace::PosX;
  origin.shell = world.shell_count() - 1;
  const int32_t hc =
      world.shell_config(origin.shell).horizontal_res / cfg.chunk_size;
  origin.chunk = glm::ivec3(hc - 1, 1, 2);

  BlockAddress crossed{};
  assert(world.offset_chunk_address(origin, 1, 0, 0, crossed));
  assert(crossed.sector != origin.sector);
  assert(crossed.chunk.y == origin.chunk.y);

  std::vector<BlockAddress> stream{};
  world.collect_stream_chunks(origin, origin.shell, 1, stream);
  bool has_other_sector = false;
  for (const BlockAddress &addr : stream) {
    if (addr.sector != origin.sector) {
      has_other_sector = true;
      break;
    }
  }
  assert(has_other_sector);
}

void test_block_world_cross_sector_neighbor_lands_on_destination_edge() {
  BlockWorldConfig cfg{};
  cfg.planet.radius = 50.0;
  cfg.surface_shells = 4;
  cfg.block_size = 1.0;
  cfg.chunk_size = 16;
  BlockWorld world{};
  world.init(cfg);

  BlockAddress origin{};
  origin.sector = PlanetFace::PosX;
  origin.shell = world.shell_count() - 1;
  const int32_t hc =
      world.shell_config(origin.shell).horizontal_res / cfg.chunk_size;
  origin.chunk = glm::ivec3(hc - 1, 0, 3);
  origin.block = glm::ivec3(cfg.chunk_size - 1, 0, 7);

  const std::vector<BlockNeighbor> result =
      world.neighbors(origin, BlockDir::Right);
  assert(result.size() == 1);
  assert(result[0].address.sector == PlanetFace::PosZ);
  assert(result[0].address.chunk.x == hc - 1);
  assert(result[0].address.block.x == cfg.chunk_size - 1);
}

void test_solar_system_has_nearby_companion_planet() {
  SolarSystem solar_system{};
  solar_system.init(64.0);
  solar_system.update(0.0);

  assert(solar_system.body_count() == 4);
  const CelestialBody &voxov = solar_system.bodies()[1];
  const CelestialBody &aster = solar_system.bodies()[3];
  assert(aster.name == "Aster");
  assert(aster.parent_index == 1);
  assert(aster.orbital.radius == 32.0);
  assert(glm::length(aster.position - voxov.position) > 2'900.0);
  assert(glm::length(aster.position - voxov.position) < 3'200.0);
}

void test_cube_edge_pairings_preserve_direction() {
  const auto edge_uv = [](CubeEdge edge, double along) {
    switch (edge) {
    case CubeEdge::Left:   return glm::dvec2(-1.0, along);
    case CubeEdge::Right:  return glm::dvec2(1.0, along);
    case CubeEdge::Top:    return glm::dvec2(along, 1.0);
    case CubeEdge::Bottom: return glm::dvec2(along, -1.0);
    }
    return glm::dvec2(0.0);
  };

  for (const CubeEdgePairing &pairing : BlockWorld::all_edge_pairings()) {
    for (const double along : {-0.63, 0.27}) {
      const glm::dvec2 source_uv = edge_uv(pairing.from_edge, along);
      double dest_u = pairing.swap_uv ? source_uv.y : source_uv.x;
      double dest_v = pairing.swap_uv ? source_uv.x : source_uv.y;
      if (pairing.flip_u) dest_u = -dest_u;
      if (pairing.flip_v) dest_v = -dest_v;

      const glm::dvec3 source = face_uv_to_direction(
          pairing.from_face, source_uv.x, source_uv.y);
      const glm::dvec3 dest = face_uv_to_direction(
          pairing.to_face, dest_u, dest_v);
      assert(glm::length(source - dest) < 1.0e-9);
    }
  }
}

void test_surface_height_matches_quantized_surface_block() {
  BlockWorldConfig cfg{};
  cfg.planet.radius = 50.0;
  cfg.surface_shells = 8;
  cfg.block_size = 1.0;
  cfg.chunk_size = 16;
  cfg.seed = 42;
  BlockWorld world{};
  world.init(cfg);

  const glm::dvec3 direction = glm::normalize(glm::dvec3(1.0, 0.31, -0.22));
  const int32_t layer = world.terrain_height_at(direction);
  const ShellConfig &surface = world.shell_config(world.shell_count() - 1);
  const double shell_layer_size =
      (surface.outer_radius - surface.inner_radius) /
      static_cast<double>(surface.vertical_layers);
  const double height = world.surface_height_above_base(direction);
  const double lower = static_cast<double>(layer) * shell_layer_size;
  const double upper = static_cast<double>(layer + 1) * shell_layer_size;
  assert(height >= lower);
  assert(height <= upper);
  assert(height <= world.max_surface_height_above_base());
}

void test_grass_mesh_is_deterministic_and_surface_aligned() {
  BlockWorldConfig cfg{};
  cfg.planet.radius = 64.0;
  cfg.planet.seed = 0x12345678ull;
  cfg.seed = cfg.planet.seed;
  cfg.surface_shells = 2;
  cfg.base_resolution = 32;
  cfg.chunk_size = 16;
  cfg.terrain_min_height = 1;
  cfg.terrain_max_height = 8;

  BlockWorld world{};
  world.init(cfg);
  const glm::dvec3 direction(1.0, 0.0, 0.0);
  BlockAddress chunk = world.address_from_world(
      cfg.planet.center + direction * world.surface_radial_distance(direction));
  chunk.block = glm::ivec3(0);
  VoxelChunk &voxels = world.get_or_generate_chunk(chunk);

  const RenderMesh first = vegetation::build_grass_mesh(
      world, chunk, voxels, glm::dvec3(0.0));
  const RenderMesh second = vegetation::build_grass_mesh(
      world, chunk, voxels, glm::dvec3(0.0));

  assert(first.mesh_id == second.mesh_id);
  assert(first.content_hash == second.content_hash);
  assert(first.vertices.size() == second.vertices.size());
  assert(first.indices == second.indices);
  assert(first.material == vegetation::kBillboardVegetationMaterial);
  assert(!first.double_sided);
  assert(!first.vertices.empty());

  for (const RenderVertex &vertex : first.vertices) {
    const glm::dvec3 world_position = glm::dvec3(vertex.position);
    assert(glm::length(glm::dvec3(vertex.normal)) > 0.99);
    assert(glm::length(world_position) > 1.0);
    const uint8_t layer = static_cast<uint8_t>(vertex.texcoord.z);
    assert(layer == vegetation::kGrassLayer ||
           layer == vegetation::kFlowerLayer ||
           layer == vegetation::kShrubLayer ||
           layer == vegetation::kFernLayer);
  }
  for (size_t i = 0; i + 3 < first.vertices.size(); i += 4) {
    assert(first.vertices[i].texcoord.y == 1.0f);
    assert(first.vertices[i + 1].texcoord.y == 1.0f);
    assert(first.vertices[i + 2].texcoord.y == 0.0f);
    assert(first.vertices[i + 3].texcoord.y == 0.0f);
  }
  for (size_t i = 0; i + 2 < first.indices.size(); i += 3) {
    const RenderVertex &a = first.vertices[first.indices[i]];
    const RenderVertex &b = first.vertices[first.indices[i + 1]];
    const RenderVertex &c = first.vertices[first.indices[i + 2]];
    const glm::vec3 face_normal = glm::normalize(
        glm::cross(b.position - a.position, c.position - a.position));
    assert(glm::dot(face_normal, a.normal) > 0.5f);
  }
}

void test_planet_quadtree_roots_are_stable() {
  PlanetDefinition planet{};
  planet.radius = 512.0;
  planet.voxel_size = 2.0;

  PlanetQuadtree quadtree;
  quadtree.init(planet, 3);

  assert(quadtree.node_count() == 6);
  const PlanetFace faces[] = {PlanetFace::PosX, PlanetFace::NegX,
                              PlanetFace::PosY, PlanetFace::NegY,
                              PlanetFace::PosZ, PlanetFace::NegZ};
  for (int i = 0; i < 6; ++i) {
    const int32_t root = quadtree.root_index(faces[i]);
    assert(root == i);

    const PlanetQuadtreeNode *node = quadtree.node(root);
    assert(node != nullptr);
    const PlanetChunkId expected_id{faces[i], 0, 0, 0};
    assert(node->id == expected_id);
    assert(node->parent == -1);
    assert(node->state == QuadtreeNodeState::Empty);
    assert(node->geometric_error > 0.0f);
    assert(node->bounds_min.x <= node->bounds_max.x);
    assert(node->bounds_min.y <= node->bounds_max.y);
    assert(node->bounds_min.z <= node->bounds_max.z);
    for (const int32_t child : node->children) {
      assert(child == -1);
    }
  }

  assert(quadtree.node(-1) == nullptr);
  assert(quadtree.node(99) == nullptr);
}

void test_planet_quadtree_subdivision_child_ids() {
  PlanetDefinition planet{};
  planet.radius = 256.0;
  planet.voxel_size = 1.0;

  PlanetQuadtree quadtree;
  quadtree.init(planet, 1);

  const int32_t root = quadtree.root_index(PlanetFace::NegZ);
  assert(quadtree.subdivide(root));
  assert(quadtree.node_count() == 10);

  const PlanetQuadtreeNode *parent = quadtree.node(root);
  assert(parent != nullptr);
  const PlanetChunkId expected_ids[] = {
      {PlanetFace::NegZ, 0, 0, 1},
      {PlanetFace::NegZ, 1, 0, 1},
      {PlanetFace::NegZ, 0, 1, 1},
      {PlanetFace::NegZ, 1, 1, 1},
  };
  for (int i = 0; i < 4; ++i) {
    const int32_t child_index = parent->children[static_cast<size_t>(i)];
    const PlanetQuadtreeNode *child = quadtree.node(child_index);
    assert(child != nullptr);
    assert(child->id == expected_ids[i]);
    assert(child->parent == root);
    assert(child->geometric_error < parent->geometric_error);
  }

  assert(quadtree.subdivide(root));
  assert(quadtree.node_count() == 10);
  assert(!quadtree.subdivide(parent->children[0]));
  assert(!quadtree.subdivide(-1));
}

struct TerrainUvBounds {
  double min_u = 0.0;
  double max_u = 0.0;
  double min_v = 0.0;
  double max_v = 0.0;
};

TerrainUvBounds terrain_mesh_uv_bounds(const PlanetDefinition &planet,
                                       const RenderMesh &mesh,
                                       PlanetFace expected_face) {
  assert(!mesh.vertices.empty());
  const PlanetFaceUV first = direction_to_face_uv(
      glm::dvec3(mesh.vertices.front().position) - planet.center);
  assert(first.face == expected_face);

  TerrainUvBounds bounds{first.u, first.u, first.v, first.v};
  for (const RenderVertex &vertex : mesh.vertices) {
    assert(std::isfinite(vertex.position.x));
    assert(std::isfinite(vertex.position.y));
    assert(std::isfinite(vertex.position.z));
    const PlanetFaceUV uv =
        direction_to_face_uv(glm::dvec3(vertex.position) - planet.center);
    assert(uv.face == expected_face);
    bounds.min_u = std::min(bounds.min_u, uv.u);
    bounds.max_u = std::max(bounds.max_u, uv.u);
    bounds.min_v = std::min(bounds.min_v, uv.v);
    bounds.max_v = std::max(bounds.max_v, uv.v);
  }
  return bounds;
}

void assert_terrain_mesh_deterministic(const RenderMesh &a,
                                       const RenderMesh &b) {
  assert(!a.vertices.empty());
  assert(!a.indices.empty());
  assert(a.indices.size() % 3 == 0);
  assert(a.vertices.size() == b.vertices.size());
  assert(a.indices.size() == b.indices.size());
  assert(a.mesh_id == b.mesh_id);
  assert(a.material == b.material);
  assert(a.mesh_id != 0);

  for (size_t i = 0; i < a.vertices.size(); ++i) {
    assert(glm::length(a.vertices[i].position - b.vertices[i].position) <
           0.0001f);
    assert(glm::length(a.vertices[i].normal - b.vertices[i].normal) <
           0.0001f);
    assert(glm::length(a.vertices[i].color - b.vertices[i].color) < 0.0001f);
  }
  for (size_t i = 0; i < a.indices.size(); ++i) {
    assert(a.indices[i] == b.indices[i]);
    assert(a.indices[i] < a.vertices.size());
  }
}

void test_planet_terrain_root_chunk_covers_face() {
  PlanetDefinition planet{};
  planet.radius = 128.0;
  planet.voxel_size = 0.5;
  planet.chunks_per_face = 1;
  planet.seed = 0x12345678u;

  const PlanetChunkId root{PlanetFace::PosY, 0, 0, 0};
  const RenderMesh mesh_a = build_single_face_planet_terrain_mesh(planet, root);
  const RenderMesh mesh_b = build_single_face_planet_terrain_mesh(planet, root);
  assert_terrain_mesh_deterministic(mesh_a, mesh_b);

  const TerrainUvBounds bounds =
      terrain_mesh_uv_bounds(planet, mesh_a, PlanetFace::PosY);
  assert(bounds.min_u <= -0.99);
  assert(bounds.max_u >= 0.99);
  assert(bounds.min_v <= -0.99);
  assert(bounds.max_v >= 0.99);

  bool has_radial_normal = false;
  for (const RenderVertex &vertex : mesh_a.vertices) {
    const glm::vec3 radial =
        glm::normalize(vertex.position - glm::vec3(planet.center));
    if (glm::dot(glm::normalize(vertex.normal), radial) > 0.98f) {
      has_radial_normal = true;
      break;
    }
  }
  assert(has_radial_normal);
}

void test_planet_terrain_lod_chunks_cover_expected_regions() {
  PlanetDefinition planet{};
  planet.radius = 128.0;
  planet.voxel_size = 0.5;
  planet.chunks_per_face = 2;
  planet.seed = 0xabcdef01u;

  const PlanetChunkId lower_left{PlanetFace::PosZ, 0, 0, 1};
  const PlanetChunkId upper_right{PlanetFace::PosZ, 1, 1, 1};
  const RenderMesh lower_mesh =
      build_single_face_planet_terrain_mesh(planet, lower_left);
  const RenderMesh upper_mesh =
      build_single_face_planet_terrain_mesh(planet, upper_right);
  assert(!lower_mesh.vertices.empty());
  assert(!upper_mesh.vertices.empty());

  const TerrainUvBounds lower =
      terrain_mesh_uv_bounds(planet, lower_mesh, PlanetFace::PosZ);
  const TerrainUvBounds upper =
      terrain_mesh_uv_bounds(planet, upper_mesh, PlanetFace::PosZ);
  constexpr double k_uv_epsilon = 0.015;
  assert(lower.min_u >= -1.0 - k_uv_epsilon);
  assert(lower.max_u <= 0.0 + k_uv_epsilon);
  assert(lower.min_v >= -1.0 - k_uv_epsilon);
  assert(lower.max_v <= 0.0 + k_uv_epsilon);
  assert(upper.min_u >= 0.0 - k_uv_epsilon);
  assert(upper.max_u <= 1.0 + k_uv_epsilon);
  assert(upper.min_v >= 0.0 - k_uv_epsilon);
  assert(upper.max_v <= 1.0 + k_uv_epsilon);

  assert(lower.max_u <= upper.min_u + k_uv_epsilon);
  assert(lower.max_v <= upper.min_v + k_uv_epsilon);
}

void test_planet_surface_flat_mesh_uses_local_plane() {
  PlanetDefinition planet{};
  planet.radius = 128.0;
  planet.voxel_size = 1.0;
  planet.chunks_per_face = 1;
  planet.seed = 0x10203040u;

  PlanetSurfaceRenderFrame surface_frame{};
  surface_frame.face = PlanetFace::PosY;
  surface_frame.camera_local_origin = glm::dvec3(0.0);
  surface_frame.distortion_scale = 1.0;

  const RenderMesh mesh = build_planet_terrain_mesh(
      planet, PlanetChunkId{PlanetFace::PosY, 0, 0, 0},
      PlanetTerrainRenderMode::SurfaceFlatFace, surface_frame);
  assert(!mesh.vertices.empty());

  float min_x = 100000.0f;
  float max_x = -100000.0f;
  float min_z = 100000.0f;
  float max_z = -100000.0f;
  for (const RenderVertex &vertex : mesh.vertices) {
    min_x = std::min(min_x, vertex.position.x);
    max_x = std::max(max_x, vertex.position.x);
    min_z = std::min(min_z, vertex.position.z);
    max_z = std::max(max_z, vertex.position.z);
    assert(std::fabs(vertex.normal.x) < 1.001f);
    assert(std::fabs(vertex.normal.y) < 1.001f);
    assert(std::fabs(vertex.normal.z) < 1.001f);
  }

  assert(min_x >= -0.001f);
  assert(max_x <= 16.001f);
  assert(min_z >= -0.001f);
  assert(max_z <= 16.001f);
  assert(max_x - min_x >= 15.0f);
  assert(max_z - min_z >= 15.0f);
}

void test_planet_impostor_is_complete_sphere_lod() {
  PlanetDefinition planet{};
  planet.radius = 256.0;
  planet.seed = 0x31415926u;

  constexpr int32_t subdivisions = 8;
  const RenderMesh mesh =
      build_planet_impostor_mesh(planet, subdivisions);
  assert(mesh.vertices.size() ==
         static_cast<size_t>(6 * subdivisions * subdivisions * 4));
  assert(mesh.indices.size() ==
         static_cast<size_t>(6 * subdivisions * subdivisions * 6));

  glm::vec3 min_color(1.0f);
  glm::vec3 max_color(0.0f);
  for (const RenderVertex &vertex : mesh.vertices) {
    assert(std::fabs(glm::length(vertex.position) - 256.0f) < 0.05f);
    assert(glm::dot(glm::normalize(vertex.position),
                    glm::normalize(vertex.normal)) > 0.999f);
    min_color = glm::min(min_color, vertex.color);
    max_color = glm::max(max_color, vertex.color);
  }
  assert(glm::length(max_color - min_color) > 0.2f);
}

void test_compact_planet_surface_is_complete_and_seamless() {
  BlockWorldConfig config{};
  config.planet.center = glm::dvec3(17.0, -4.0, 9.0);
  config.planet.radius = 64.0;
  config.planet.seed = 0x31415926u;
  config.surface_shells = 2;
  config.base_resolution = 32;
  config.block_size = 1.0;
  config.terrain_feature_size = 24.0;
  config.terrain_base_height = 4.0f;
  config.terrain_amplitude = 3.0f;
  config.terrain_min_height = 1;
  config.terrain_max_height = 8;
  config.chunk_size = 16;
  config.seed = config.planet.seed;
  BlockWorld world;
  world.init(config);

  constexpr int32_t grid = 16;
  constexpr double bias = 0.20;
  const RenderMesh mesh =
      build_compact_planet_surface_mesh(world, grid, bias);
  assert(mesh.use_16_bit_indices);
  assert(mesh.indices.empty());
  assert(mesh.vertices.size() ==
         static_cast<size_t>(6 * (grid + 1) * (grid + 1)));
  assert(mesh.indices16.size() ==
         static_cast<size_t>(6 * grid * grid * 6));
  assert(mesh.content_hash != 0);
  assert(glm::distance(mesh.world_origin, config.planet.center) < 1.0e-9);

  for (const RenderVertex &vertex : mesh.vertices) {
    const glm::dvec3 relative(vertex.position);
    const glm::dvec3 direction = glm::normalize(relative);
    const double expected_radius = config.planet.radius + std::max(
        0.0, world.surface_height_above_base(direction) - bias);
    assert(std::abs(glm::length(relative) - expected_radius) < 1.0e-4);
    assert(glm::dot(glm::normalize(vertex.normal), glm::vec3(direction)) >
           0.999f);
    assert(vertex.texcoord.z == 0.0f);
  }

  for (size_t i = 0; i < mesh.indices16.size(); i += 3) {
    const glm::dvec3 a(mesh.vertices[mesh.indices16[i]].position);
    const glm::dvec3 b(mesh.vertices[mesh.indices16[i + 1]].position);
    const glm::dvec3 c(mesh.vertices[mesh.indices16[i + 2]].position);
    const glm::dvec3 normal = glm::cross(b - a, c - a);
    assert(glm::dot(normal, a + b + c) > 0.0);
  }

  auto face_index = [](PlanetFace face) -> int32_t {
    switch (face) {
    case PlanetFace::PosX: return 0;
    case PlanetFace::NegX: return 1;
    case PlanetFace::PosY: return 2;
    case PlanetFace::NegY: return 3;
    case PlanetFace::PosZ: return 4;
    case PlanetFace::NegZ: return 5;
    }
    return 0;
  };
  auto vertex_at_uv = [&](PlanetFace face, double u, double v)
      -> const RenderVertex & {
    const int32_t x = std::clamp(
        static_cast<int32_t>(std::lround((u + 1.0) * 0.5 * grid)), 0, grid);
    const int32_t z = std::clamp(
        static_cast<int32_t>(std::lround((v + 1.0) * 0.5 * grid)), 0, grid);
    const size_t face_base = static_cast<size_t>(face_index(face)) *
                             static_cast<size_t>((grid + 1) * (grid + 1));
    return mesh.vertices[face_base +
                         static_cast<size_t>(z * (grid + 1) + x)];
  };
  auto edge_uv = [](CubeEdge edge, double along) {
    switch (edge) {
    case CubeEdge::Left: return glm::dvec2(-1.0, along);
    case CubeEdge::Right: return glm::dvec2(1.0, along);
    case CubeEdge::Top: return glm::dvec2(along, 1.0);
    case CubeEdge::Bottom: return glm::dvec2(along, -1.0);
    }
    return glm::dvec2(0.0);
  };

  for (const CubeEdgePairing &pairing : BlockWorld::all_edge_pairings()) {
    for (int32_t step = 0; step <= grid; ++step) {
      const double along = -1.0 + 2.0 * static_cast<double>(step) / grid;
      const glm::dvec2 source_uv = edge_uv(pairing.from_edge, along);
      double dest_u = pairing.swap_uv ? source_uv.y : source_uv.x;
      double dest_v = pairing.swap_uv ? source_uv.x : source_uv.y;
      if (pairing.flip_u) dest_u = -dest_u;
      if (pairing.flip_v) dest_v = -dest_v;
      const RenderVertex &source = vertex_at_uv(
          pairing.from_face, source_uv.x, source_uv.y);
      const RenderVertex &dest = vertex_at_uv(
          pairing.to_face, dest_u, dest_v);
      assert(glm::distance(source.position, dest.position) < 1.0e-4f);
    }
  }
}

void test_planet_flight_clipmap_is_camera_relative_and_textured() {
  PlanetDefinition planet{};
  planet.center = glm::dvec3(0.0);
  planet.radius = 8'192.0;
  planet.voxel_size = 1.0;
  planet.seed = 0x4d45455345u;

  BlockWorldConfig config{};
  config.planet = planet;
  config.surface_shells = 4;
  config.base_resolution = 64;
  config.block_size = 1.0;
  config.terrain_feature_size = 512.0;
  config.chunk_size = 16;
  config.seed = planet.seed;
  BlockWorld world;
  world.init(config);

  const glm::dvec3 direction(1.0, 0.0, 0.0);
  const glm::dvec3 camera_origin = direction * (planet.radius + 1'000.0);
  const RenderMesh mesh = build_planet_flight_clipmap(
      world, direction, 1'000.0, camera_origin, 16, 2);
  assert(!mesh.vertices.empty());
  assert(!mesh.indices.empty());
  assert(mesh.content_hash != 0);
  assert(glm::distance(mesh.world_origin, camera_origin) < 0.001);

  glm::vec3 bounds_min(std::numeric_limits<float>::max());
  glm::vec3 bounds_max(-std::numeric_limits<float>::max());
  for (const RenderVertex &vertex : mesh.vertices) {
    const glm::dvec3 world_position =
        glm::dvec3(vertex.position) + camera_origin;
    const double radial_distance = glm::length(world_position - planet.center);
    assert(radial_distance >= planet.radius - 1.0);
    assert(radial_distance <= planet.radius + 1'000.0);
    assert(vertex.texcoord.z >= 0.0f);
    bounds_min = glm::min(bounds_min, vertex.position);
    bounds_max = glm::max(bounds_max, vertex.position);
  }
  assert(bounds_max.y - bounds_min.y > 1'000.0f);
  assert(bounds_max.z - bounds_min.z > 1'000.0f);
}

void test_planet_streamer_returns_runtime_terrain_chunks() {
  PlanetDefinition planet{};
  planet.center = glm::dvec3(0.0);
  planet.radius = 128.0;
  planet.voxel_size = 0.5;
  planet.chunks_per_face = 1;
  planet.seed = 0x55667788u;

  const glm::dvec3 camera_pos =
      planet.center + glm::dvec3(0.0, 32.0, planet.radius * 4.0);
  const glm::mat4 view =
      glm::lookAt(glm::vec3(camera_pos), glm::vec3(planet.center),
                  glm::vec3(0.0f, 1.0f, 0.0f));
  const glm::mat4 projection =
      glm::perspective(glm::radians(70.0f), 16.0f / 9.0f, 0.1f, 4096.0f);

  PlanetStreamer streamer;
  streamer.set_generation_budget_per_update(32);

  PlanetRenderRequest request{};
  request.planet = planet;
  request.max_lod = 0;
  request.camera_world_position = camera_pos;
  request.view_projection = projection * view;
  request.screen_height_pixels = 1080.0f;

  const std::vector<RenderMesh> meshes_a = streamer.update(request);
  assert(!meshes_a.empty());
  assert(meshes_a.size() == streamer.render_meshes().size());

  std::vector<uint64_t> first_mesh_ids;
  first_mesh_ids.reserve(meshes_a.size());
  for (const RenderMesh &mesh : meshes_a) {
    assert(!mesh.vertices.empty());
    assert(!mesh.indices.empty());
    assert(mesh.mesh_id != 0);
    first_mesh_ids.push_back(mesh.mesh_id);
  }

  int resident_root_count = 0;
  const PlanetFace faces[] = {PlanetFace::PosX, PlanetFace::NegX,
                              PlanetFace::PosY, PlanetFace::NegY,
                              PlanetFace::PosZ, PlanetFace::NegZ};
  for (const PlanetFace face : faces) {
    const PlanetChunkId id{face, 0, 0, 0};
    const RenderMesh *resident = streamer.resident_mesh(id);
    if (resident == nullptr) {
      continue;
    }

    RenderMesh expected = build_single_face_planet_terrain_mesh(planet, id);
    expected.mesh_id = PlanetStreamer::stable_mesh_id(id);
    assert_terrain_mesh_deterministic(*resident, expected);
    ++resident_root_count;
  }
  assert(resident_root_count > 0);

  const std::vector<RenderMesh> meshes_b = streamer.update(request);
  assert(meshes_b.size() == first_mesh_ids.size());
  for (size_t i = 0; i < meshes_b.size(); ++i) {
    assert(meshes_b[i].mesh_id == first_mesh_ids[i]);
  }
}

void test_planet_streamer_refines_near_surface() {
  PlanetDefinition planet{};
  planet.center = glm::dvec3(0.0);
  planet.radius = 128.0;
  planet.voxel_size = 1.0;
  planet.chunks_per_face = 1;
  planet.seed = 0x77889900u;

  const glm::dvec3 camera_pos =
      planet.center + glm::dvec3(0.0, planet.radius + 8.0, 0.0);
  const glm::mat4 view =
      glm::lookAt(glm::vec3(camera_pos), glm::vec3(16.0f, 128.0f, 16.0f),
                  glm::vec3(0.0f, 0.0f, 1.0f));
  const glm::mat4 projection =
      glm::perspective(glm::radians(70.0f), 16.0f / 9.0f, 0.1f, 2048.0f);

  PlanetStreamer streamer;
  streamer.set_config(PlanetStreamerConfig{
      .generation_budget_per_update = 256,
      .max_visible_chunks = 256,
      .lod_error_threshold_pixels = 2.0f,
  });

  PlanetRenderRequest request{};
  request.planet = planet;
  request.max_lod = 6;
  request.camera_world_position = camera_pos;
  request.view_projection = projection * view;
  request.screen_height_pixels = 1080.0f;

  for (int i = 0; i < 12; ++i) {
    streamer.update(request);
  }

  int32_t deepest_resident_lod = 0;
  for (int32_t i = 0; i < streamer.quadtree().node_count(); ++i) {
    const PlanetQuadtreeNode *node = streamer.quadtree().node(i);
    if (node != nullptr && streamer.is_chunk_resident(node->id)) {
      deepest_resident_lod = std::max(deepest_resident_lod, node->id.lod);
    }
  }

  assert(!streamer.render_meshes().empty());
  assert(deepest_resident_lod >= 4);
}

void test_planet_streamer_reports_budgeted_stats_and_revision() {
  PlanetDefinition planet{};
  planet.center = glm::dvec3(0.0);
  planet.radius = 128.0;
  planet.voxel_size = 1.0;
  planet.chunks_per_face = 1;
  planet.seed = 0x10203040u;

  const glm::dvec3 camera_pos =
      planet.center + glm::dvec3(0.0, 32.0, planet.radius * 4.0);
  const glm::mat4 view =
      glm::lookAt(glm::vec3(camera_pos), glm::vec3(planet.center),
                  glm::vec3(0.0f, 1.0f, 0.0f));
  const glm::mat4 projection =
      glm::perspective(glm::radians(70.0f), 16.0f / 9.0f, 0.1f, 4096.0f);

  PlanetStreamer streamer;
  streamer.set_config(PlanetStreamerConfig{
      .generation_budget_per_update = 1,
      .max_visible_chunks = 256,
      .lod_error_threshold_pixels = 2.0f,
  });

  PlanetRenderRequest request{};
  request.planet = planet;
  request.max_lod = 0;
  request.camera_world_position = camera_pos;
  request.view_projection = projection * view;
  request.screen_height_pixels = 1080.0f;

  streamer.update(request);
  const PlanetStreamerStats first_stats = streamer.stats();
  assert(first_stats.requested_chunk_count > first_stats.generated_chunk_count);
  assert(first_stats.generated_chunk_count == 1);
  assert(first_stats.resident_chunk_count == streamer.streamed_chunk_count());
  assert(first_stats.visible_chunk_count == streamer.render_meshes().size());

  streamer.set_generation_budget_per_update(32);
  streamer.update(request);
  assert(streamer.stats().resident_chunk_count == streamer.streamed_chunk_count());
  assert(streamer.stats().visible_chunk_count == streamer.render_meshes().size());

  const uint64_t stable_revision = streamer.mesh_set_revision();
  streamer.update(request);
  assert(streamer.stats().requested_chunk_count == 0);
  assert(streamer.stats().generated_chunk_count == 0);
  assert(streamer.stats().evicted_chunk_count == 0);
  assert(streamer.stats().resident_chunk_count == streamer.streamed_chunk_count());
  assert(streamer.stats().visible_chunk_count == streamer.render_meshes().size());
  assert(streamer.mesh_set_revision() == stable_revision);
}

void test_planet_streamer_reports_evicted_chunks() {
  PlanetDefinition planet{};
  planet.center = glm::dvec3(0.0);
  planet.radius = 128.0;
  planet.voxel_size = 1.0;
  planet.chunks_per_face = 1;
  planet.seed = 0x50607080u;

  const glm::dvec3 camera_pos =
      planet.center + glm::dvec3(0.0, 32.0, planet.radius * 4.0);
  const glm::mat4 toward_view =
      glm::lookAt(glm::vec3(camera_pos), glm::vec3(planet.center),
                  glm::vec3(0.0f, 1.0f, 0.0f));
  const glm::mat4 away_view =
      glm::lookAt(glm::vec3(camera_pos),
                  glm::vec3(camera_pos + glm::dvec3(0.0, 0.0, 1.0)),
                  glm::vec3(0.0f, 1.0f, 0.0f));
  const glm::mat4 projection =
      glm::perspective(glm::radians(70.0f), 16.0f / 9.0f, 0.1f, 4096.0f);

  PlanetStreamer streamer;
  streamer.set_config(PlanetStreamerConfig{
      .generation_budget_per_update = 32,
      .max_visible_chunks = 256,
      .lod_error_threshold_pixels = 2.0f,
  });

  PlanetRenderRequest request{};
  request.planet = planet;
  request.max_lod = 0;
  request.camera_world_position = camera_pos;
  request.view_projection = projection * toward_view;
  request.screen_height_pixels = 1080.0f;
  streamer.update(request);
  assert(streamer.streamed_chunk_count() > 0);

  request.view_projection = projection * away_view;
  uint32_t evicted = 0;
  for (int i = 0; i < 601; ++i) {
    streamer.update(request);
    evicted += streamer.stats().evicted_chunk_count;
  }

  assert(evicted > 0);
  assert(streamer.stats().resident_chunk_count == streamer.streamed_chunk_count());
  assert(streamer.stats().visible_chunk_count == streamer.render_meshes().size());
}

void test_atmosphere_transition_manager_descent_and_ascent() {
  PlanetDefinition planet{};
  planet.radius = 100.0;

  AtmosphereTransitionManager manager;
  manager.configure(AtmosphereTransitionConfig{
      .surface_altitude = 12.0,
      .space_altitude = 24.0,
      .fade_seconds = 1.0,
  });
  manager.reset_to_space();

  const glm::dvec3 descent_pos = planet.center + glm::dvec3(0.0, 110.0, 0.0);
  manager.update(planet, descent_pos, glm::dvec3(0.0, -5.0, 0.0), 0.0);
  assert(manager.snapshot().state == PlanetRenderState::Descending);
  assert(manager.snapshot().velocity_frozen);
  assert(manager.snapshot().active_face == PlanetFace::PosY);

  manager.update(planet, descent_pos, glm::dvec3(0.0), 1.0);
  assert(manager.snapshot().state == PlanetRenderState::Surface);
  assert(manager.snapshot().surface_alpha == 1.0);
  assert(manager.snapshot().surface_physics_active);

  const glm::dvec3 ascent_pos = planet.center + glm::dvec3(0.0, 126.0, 0.0);
  manager.update(planet, ascent_pos, glm::dvec3(0.0, 5.0, 0.0), 0.0);
  assert(manager.snapshot().state == PlanetRenderState::Ascending);
  manager.update(planet, ascent_pos, glm::dvec3(0.0), 1.0);
  assert(manager.snapshot().state == PlanetRenderState::Space);
  assert(manager.snapshot().space_alpha == 1.0);
}

void test_planet_surface_raycast_radial_down_and_up() {
  VoxelCollisionWorld collision_world;
  const glm::vec3 center(3.0f, -2.0f, 5.0f);
  const float radius = 64.0f;
  collision_world.set_planet_surface_collider(center, radius);
  assert(collision_world.has_planet_surface_collider());

  const glm::vec3 up = glm::normalize(glm::vec3(0.25f, 1.0f, -0.5f));
  const glm::vec3 origin = center + up * (radius + 12.0f);

  float hit_distance = 0.0f;
  assert(collision_world.raycast(origin, -up, 40.0f, hit_distance));
  assert(std::fabs(hit_distance - 12.0f) < 0.05f);

  assert(!collision_world.raycast(origin, up, 40.0f, hit_distance));

  glm::vec3 surface_point(0.0f);
  glm::vec3 surface_up(0.0f);
  assert(collision_world.planet_surface_point(origin, surface_point,
                                              surface_up));
  assert(glm::length(surface_up - up) < 0.0001f);
  assert(std::fabs(glm::length(surface_point - center) - radius) < 0.001f);
}

void test_net_header_validation() {
  const NetPacketHeader ok =
      net_make_header(NetMsgType::Snapshot,
                      static_cast<uint16_t>(sizeof(NetSnapshot)), 12u, 0u);
  assert(net_header_basic_valid(ok));

  NetPacketHeader wrong_magic = ok;
  wrong_magic.magic ^= 0x1u;
  assert(!net_header_basic_valid(wrong_magic));

  NetPacketHeader wrong_version = ok;
  wrong_version.version = 99u;
  assert(!net_header_basic_valid(wrong_version));

  NetPacketHeader wrong_type = ok;
  wrong_type.type = 0;
  assert(!net_header_basic_valid(wrong_type));

  NetPacketHeader too_big_payload = ok;
  too_big_payload.payload_size =
      static_cast<uint16_t>(k_net_max_payload_bytes + 1u);
  assert(!net_header_basic_valid(too_big_payload));
}

void test_chunk_meshing() {
  VoxelChunk chunk;
  chunk.generate_heightmap_terrain();
  RenderMesh mesh = chunk.build_greedy_mesh();

  assert(!mesh.vertices.empty());
  assert(!mesh.indices.empty());
  assert(mesh.indices.size() % 3 == 0);
  bool has_surface_normal = false;
  for (const RenderVertex &vertex : mesh.vertices) {
    if (glm::length(vertex.normal) > 0.5f) {
      has_surface_normal = true;
      break;
    }
  }
  assert(has_surface_normal);
}

void test_chunk_world_footprint() {
  VoxelChunk chunk;
  chunk.generate_flat_ground(0);
  const RenderMesh mesh = chunk.build_greedy_mesh();
  assert(!mesh.vertices.empty());

  float max_x = -1000.0f;
  float max_z = -1000.0f;
  for (const RenderVertex &v : mesh.vertices) {
    max_x = std::max(max_x, v.position.x);
    max_z = std::max(max_z, v.position.z);
  }

  assert(max_x >= static_cast<float>(VoxelChunk::CHUNK_X) - 0.001f);
  assert(max_z >= static_cast<float>(VoxelChunk::CHUNK_Z) - 0.001f);
  assert(max_x <= static_cast<float>(VoxelChunk::CHUNK_X) + 0.001f);
  assert(max_z <= static_cast<float>(VoxelChunk::CHUNK_Z) + 0.001f);
}

void test_single_voxel_mesh_bounds() {
  VoxelChunk chunk;
  chunk.set_solid(0, 0, 0, true);
  assert(!chunk.solid(-1, 0, 0));
  assert(!chunk.solid(0, -1, 0));
  assert(!chunk.solid(0, 0, -1));
  assert(!chunk.solid(VoxelChunk::CHUNK_X, 0, 0));
  assert(!chunk.solid(0, VoxelChunk::CHUNK_Y, 0));
  assert(!chunk.solid(0, 0, VoxelChunk::CHUNK_Z));
  const RenderMesh mesh = chunk.build_greedy_mesh();

  assert(mesh.vertices.size() == 24);
  assert(mesh.indices.size() == 36);

  glm::vec3 min_pos(1000.0f);
  glm::vec3 max_pos(-1000.0f);
  for (const RenderVertex &vertex : mesh.vertices) {
    min_pos = glm::min(min_pos, vertex.position);
    max_pos = glm::max(max_pos, vertex.position);
  }

  assert(std::fabs(min_pos.x - 0.0f) < 0.001f);
  assert(std::fabs(min_pos.y - 0.0f) < 0.001f);
  assert(std::fabs(min_pos.z - 0.0f) < 0.001f);
  assert(std::fabs(max_pos.x - 1.0f) < 0.001f);
  assert(std::fabs(max_pos.y - 1.0f) < 0.001f);
  assert(std::fabs(max_pos.z - 1.0f) < 0.001f);
}

void test_chunk_seed_determinism() {
  VoxelChunk a;
  VoxelChunk b;
  VoxelChunk c;

  a.generate_heightmap_terrain_seeded(12345u, 4, -2);
  b.generate_heightmap_terrain_seeded(12345u, 4, -2);
  c.generate_heightmap_terrain_seeded(12345u, 5, -2);

  const RenderMesh mesh_a = a.build_greedy_mesh();
  const RenderMesh mesh_b = b.build_greedy_mesh();
  const RenderMesh mesh_c = c.build_greedy_mesh();

  assert(mesh_a.vertices.size() == mesh_b.vertices.size());
  assert(mesh_a.indices.size() == mesh_b.indices.size());
  for (size_t i = 0; i < mesh_a.vertices.size(); ++i) {
    const glm::vec3 pa = mesh_a.vertices[i].position;
    const glm::vec3 pb = mesh_b.vertices[i].position;
    assert(std::fabs(pa.x - pb.x) < 0.0001f);
    assert(std::fabs(pa.y - pb.y) < 0.0001f);
    assert(std::fabs(pa.z - pb.z) < 0.0001f);
  }

  bool different = mesh_a.vertices.size() != mesh_c.vertices.size() ||
                   mesh_a.indices.size() != mesh_c.indices.size();
  if (!different && !mesh_a.vertices.empty() && !mesh_c.vertices.empty()) {
    const glm::vec3 pa = mesh_a.vertices.front().position;
    const glm::vec3 pc = mesh_c.vertices.front().position;
    different = std::fabs(pa.x - pc.x) > 0.0001f ||
                std::fabs(pa.y - pc.y) > 0.0001f ||
                std::fabs(pa.z - pc.z) > 0.0001f;
  }
  assert(different);
}

int highest_solid_y(const VoxelChunk &chunk, int x, int z) {
  for (int y = VoxelChunk::CHUNK_Y - 1; y >= 0; --y) {
    if (chunk.solid(x, y, z)) {
      return y;
    }
  }
  return -1;
}

void test_world_generator_hills_and_valleys_are_deterministic_and_bounded() {
  const WorldGenerator generator(k_voxov_flat_world_seed);
  const WorldGenerator same_seed(k_voxov_flat_world_seed);
  const WorldGenerator different_seed(k_voxov_flat_world_seed + 1u);
  float min_height = static_cast<float>(VoxelChunk::CHUNK_Y);
  float max_height = 0.0f;
  bool different_seed_changes_surface = false;
  bool includes_valley_influence = false;

  for (int z = -384; z <= 384; z += 24) {
    for (int x = -384; x <= 384; x += 24) {
      const TerrainColumnSample sample =
          generator.sample_column(static_cast<float>(x), static_cast<float>(z));
      const TerrainColumnSample repeat =
          same_seed.sample_column(static_cast<float>(x), static_cast<float>(z));
      const TerrainColumnSample other = different_seed.sample_column(
          static_cast<float>(x), static_cast<float>(z));

      assert(std::fabs(sample.surface_height - repeat.surface_height) < 0.0001f);
      assert(std::fabs(sample.valley_factor - repeat.valley_factor) < 0.0001f);
      assert(sample.surface_height >= 2.0f);
      assert(sample.surface_height <=
             static_cast<float>(VoxelChunk::CHUNK_Y - 3));
      assert(sample.valley_factor >= 0.0f && sample.valley_factor <= 1.0f);

      min_height = std::min(min_height, sample.surface_height);
      max_height = std::max(max_height, sample.surface_height);
      different_seed_changes_surface =
          different_seed_changes_surface ||
          std::fabs(sample.surface_height - other.surface_height) > 0.01f;
      includes_valley_influence =
          includes_valley_influence || sample.valley_factor > 0.5f;
    }
  }

  assert(max_height - min_height >= 8.0f);
  assert(different_seed_changes_surface);
  assert(includes_valley_influence);
}

void test_heightmap_generation_samples_continuously_across_chunk_edges() {
  const uint64_t seed = k_voxov_flat_world_seed;
  const WorldGenerator generator(seed);
  VoxelChunk left;
  VoxelChunk right;
  left.generate_heightmap_terrain_seeded(seed, 1, -2);
  right.generate_heightmap_terrain_seeded(seed, 2, -2);

  for (int z = 0; z < VoxelChunk::CHUNK_Z; ++z) {
    const float world_z =
        static_cast<float>(-2 * VoxelChunk::CHUNK_Z + z);
    const int expected_left = static_cast<int>(std::floor(
        generator.sample_height(static_cast<float>(2 * VoxelChunk::CHUNK_X - 1),
                                world_z)));
    const int expected_right = static_cast<int>(std::floor(
        generator.sample_height(static_cast<float>(2 * VoxelChunk::CHUNK_X),
                                world_z)));
    const int generated_left =
        highest_solid_y(left, VoxelChunk::CHUNK_X - 1, z);
    const int generated_right = highest_solid_y(right, 0, z);

    assert(generated_left == expected_left);
    assert(generated_right == expected_right);
    assert(std::abs(generated_left - generated_right) <= 2);
  }
}

void test_chunk_spherical_planet_generation() {
  VoxelChunk chunk;
  chunk.generate_spherical_planet_seeded(0xBEEF1234u);
  const RenderMesh mesh = chunk.build_greedy_mesh();
  assert(!mesh.vertices.empty());
  assert(!mesh.indices.empty());

  int solid_count = 0;
  for (int z = 0; z < VoxelChunk::CHUNK_Z; ++z) {
    for (int y = 0; y < VoxelChunk::CHUNK_Y; ++y) {
      for (int x = 0; x < VoxelChunk::CHUNK_X; ++x) {
        solid_count += chunk.solid(x, y, z) ? 1 : 0;
      }
    }
  }
  assert(solid_count > 0);
  assert(solid_count <
         (VoxelChunk::CHUNK_X * VoxelChunk::CHUNK_Y * VoxelChunk::CHUNK_Z) / 2);
}

void test_voxel_material_surface_assignment() {
  VoxelChunk chunk;
  chunk.generate_flat_ground(2);

  assert(chunk.material(3, 2, 4) == VoxelMaterial::Grass);
  assert(chunk.material(3, 1, 4) == VoxelMaterial::Dirt);
  assert(chunk.material(3, 0, 4) == VoxelMaterial::Dirt);
  assert(chunk.material(3, 3, 4) == VoxelMaterial::Air);
}

void test_voxel_material_custom_values_affect_mesh_colors() {
  VoxelChunk chunk;
  chunk.set_material(0, 0, 0, VoxelMaterial::Stone);
  const RenderMesh mesh = chunk.build_greedy_mesh();

  assert(!mesh.vertices.empty());
  // Stone at y=0 (height_t=0): (0.44, 0.46, 0.48) after terrain color contrast fix.
  // The block sits at the bottom of the chunk (y=0) so height_t ≈ 0.
  bool found_stone_tint = false;
  for (const RenderVertex &vertex : mesh.vertices) {
    if (std::fabs(vertex.color.r - 0.44f) < 0.001f &&
        std::fabs(vertex.color.g - 0.46f) < 0.001f &&
        std::fabs(vertex.color.b - 0.48f) < 0.001f) {
      found_stone_tint = true;
      break;
    }
  }
  assert(found_stone_tint);
}

void test_camera_yaw_response() {
  PlayerEntity player{};
  player.camera_rig.yaw = 0.0f;
  player.camera_rig.pitch = 0.0f;
  player.camera_rig.sensitivityMouse = 0.1f;

  InputState input{};
  input.look_delta.x = 10.0f;
  input.look_delta.y = 0.0f;

  const glm::vec3 forward_before =
      PlayerControllerSystem::orbit_forward_from_angles(
          player.camera_rig.yaw, player.camera_rig.pitch);
  PlayerControllerSystem::update_camera_rig(player, input, false, 1.0f / 60.0f);
  const glm::vec3 forward_after =
      PlayerControllerSystem::orbit_forward_from_angles(
          player.camera_rig.yaw, player.camera_rig.pitch);

  assert(player.camera_rig.yaw > 0.0f);
  assert(forward_after.x > forward_before.x);
}

void test_strafe_axis_sign() {
  const MovementDebug basis = PlayerControllerSystem::compute_movement_vectors(
      0.0f, glm::vec2(0.0f, 0.0f));
  assert(std::fabs(std::fabs(basis.right.x) - 1.0f) < 0.0001f);
  assert(std::fabs(basis.right.y) < 0.0001f);
  assert(std::fabs(basis.right.z) < 0.0001f);

  const float right_sign = (basis.right.x >= 0.0f) ? 1.0f : -1.0f;
  const MovementDebug move_d = PlayerControllerSystem::compute_movement_vectors(
      0.0f, glm::vec2(1.0f, 0.0f));
  const MovementDebug move_a = PlayerControllerSystem::compute_movement_vectors(
      0.0f, glm::vec2(-1.0f, 0.0f));
  assert(move_d.desired.x * right_sign > 0.0f);
  assert(move_a.desired.x * right_sign < 0.0f);

  const MovementDebug move_w = PlayerControllerSystem::compute_movement_vectors(
      0.0f, glm::vec2(0.0f, 1.0f));
  const MovementDebug move_s = PlayerControllerSystem::compute_movement_vectors(
      0.0f, glm::vec2(0.0f, -1.0f));
  assert(move_w.desired.z > 0.0f);
  assert(move_s.desired.z < 0.0f);
}

void test_player_settles_on_ground() {
  VoxelChunk chunk;
  chunk.generate_flat_ground(0);
  VoxelCollisionWorld collision_world(&chunk);

  PlayerEntity player = PlayerControllerSystem::spawn_player(collision_world);
  player.transform.position = glm::vec3(8.0f, 3.0f, 8.0f);
  player.controller.grounded = false;
  player.controller.velocity = glm::vec3(0.0f);

  InputState input{};
  for (int i = 0; i < 120; ++i) {
    PlayerControllerSystem::simulate_fixed(player, input, collision_world,
                                           1.0f / 60.0f, false);
  }

  const float settled_y = player.transform.position.y;
  assert(player.controller.grounded);
  assert(settled_y > 0.8f);
  assert(settled_y < 1.3f);

  for (int i = 0; i < 60; ++i) {
    PlayerControllerSystem::simulate_fixed(player, input, collision_world,
                                           1.0f / 60.0f, false);
  }
  assert(player.transform.position.y >= settled_y - 0.02f);
}

void test_player_animation_state_transitions() {
  VoxelChunk chunk;
  chunk.generate_flat_ground(0);
  VoxelCollisionWorld collision_world(&chunk);

  PlayerEntity player = PlayerControllerSystem::spawn_player(collision_world);
  player.transform.position = glm::vec3(8.0f, 1.05f, 8.0f);
  player.controller.grounded = true;
  player.controller.velocity = glm::vec3(0.0f);

  InputState input{};
  PlayerControllerSystem::simulate_fixed(player, input, collision_world,
                                         1.0f / 60.0f, false);
  assert(player.anim_state == PlayerAnimState::Idle);
  assert(player.animation.state == PlayerAnimState::Idle);
  assert(player.locomotion.state == PlayerLocomotionState::Idle);

  input.move = glm::vec2(0.0f, 1.0f);
  input.sprint_held = false;
  for (int i = 0; i < 6; ++i) {
    PlayerControllerSystem::simulate_fixed(player, input, collision_world,
                                           1.0f / 60.0f, false);
  }
  assert(player.anim_state == PlayerAnimState::LocomotionWalk ||
         player.anim_state == PlayerAnimState::StartMove);
  assert(player.locomotion.state == PlayerLocomotionState::Walk ||
         player.locomotion.state == PlayerLocomotionState::StartMove);

  input.sprint_held = true;
  for (int i = 0; i < 10; ++i) {
    PlayerControllerSystem::simulate_fixed(player, input, collision_world,
                                           1.0f / 60.0f, false);
  }
  assert(player.anim_state == PlayerAnimState::LocomotionRun ||
         player.anim_state == PlayerAnimState::MovingTurn);
  assert(player.locomotion.state == PlayerLocomotionState::Run ||
         player.locomotion.state == PlayerLocomotionState::MovingTurn);

  input.move = glm::vec2(0.0f);
  input.sprint_held = false;
  input.jump_pressed = true;
  player.controller.grounded = true;
  PlayerControllerSystem::simulate_fixed(player, input, collision_world,
                                         1.0f / 60.0f, false);
  assert(player.anim_state == PlayerAnimState::JumpTakeoff);
  assert(player.locomotion.state == PlayerLocomotionState::JumpStart);
}

void test_player_directional_locomotion_states() {
  VoxelChunk chunk;
  chunk.generate_flat_ground(0);
  VoxelCollisionWorld collision_world(&chunk);

  auto expect_direction = [&](glm::vec2 move,
                              bool sprint,
                              PlayerAnimState expected_anim) {
    PlayerEntity player = PlayerControllerSystem::spawn_player(collision_world);
    player.transform.position = glm::vec3(8.0f, 1.05f, 8.0f);
    player.controller.grounded = true;
    player.controller.velocity = glm::vec3(0.0f);

    InputState input{};
    input.move = move;
    input.sprint_held = sprint;
    for (int i = 0; i < 12; ++i) {
      PlayerControllerSystem::simulate_fixed(player, input, collision_world,
                                             1.0f / 60.0f, false);
    }

    assert(player.locomotion.state ==
           (sprint ? PlayerLocomotionState::Run : PlayerLocomotionState::Walk));
    assert(player.anim_state == expected_anim);
  };

  expect_direction(glm::vec2(0.0f, 1.0f), false, PlayerAnimState::LocomotionWalk);
  expect_direction(glm::vec2(0.0f, -1.0f), false,
                   PlayerAnimState::LocomotionWalkBackward);
  expect_direction(glm::vec2(-1.0f, 0.0f), false,
                   PlayerAnimState::LocomotionWalkLeft);
  expect_direction(glm::vec2(1.0f, 0.0f), false,
                   PlayerAnimState::LocomotionWalkRight);
  expect_direction(glm::vec2(1.0f, 1.0f), false,
                   PlayerAnimState::LocomotionWalkForwardRight);
  expect_direction(glm::vec2(-1.0f, -1.0f), true,
                   PlayerAnimState::LocomotionRunBackwardLeft);
}

void test_player_backward_strafe_preserves_camera_facing() {
  VoxelChunk chunk;
  chunk.generate_flat_ground(0);
  VoxelCollisionWorld collision_world(&chunk);

  PlayerEntity player = PlayerControllerSystem::spawn_player(collision_world);
  player.transform.position = glm::vec3(8.0f, 1.05f, 8.0f);
  player.controller.grounded = true;
  player.controller.velocity = glm::vec3(0.0f);
  player.camera_rig.yaw = 35.0f;
  player.locomotion.facing_yaw_deg = 35.0f;
  player.locomotion.desired_yaw_deg = 35.0f;

  InputState input{};
  input.move = glm::vec2(0.0f, -1.0f);
  for (int i = 0; i < 12; ++i) {
    PlayerControllerSystem::simulate_fixed(player, input, collision_world,
                                           1.0f / 60.0f, false);
  }

  assert(player.anim_state == PlayerAnimState::LocomotionWalkBackward);
  assert(std::fabs(player.locomotion.facing_yaw_deg - 35.0f) < 0.01f);
  assert(std::fabs(player.locomotion.turn_delta_deg) < 0.01f);
}

void test_player_coyote_jump_window() {
  VoxelChunk chunk;
  chunk.generate_flat_ground(0);
  VoxelCollisionWorld collision_world(&chunk);

  PlayerEntity player = PlayerControllerSystem::spawn_player(collision_world);
  player.transform.position = glm::vec3(8.0f, 1.05f, 8.0f);
  player.controller.grounded = true;

  InputState input{};
  PlayerControllerSystem::simulate_fixed(player, input, collision_world,
                                         1.0f / 60.0f, false);
  player.controller.grounded = false;
  player.locomotion.coyote_timer = player.locomotion_tuning.coyote_time * 0.5f;
  input.jump_pressed = true;
  PlayerControllerSystem::simulate_fixed(player, input, collision_world,
                                         1.0f / 60.0f, false);

  assert(player.locomotion.state == PlayerLocomotionState::JumpStart);
  assert(player.locomotion.vertical_velocity > 0.0f);
}

void test_player_jump_buffer_consumes_on_landing() {
  VoxelChunk chunk;
  chunk.generate_flat_ground(0);
  VoxelCollisionWorld collision_world(&chunk);

  PlayerEntity player = PlayerControllerSystem::spawn_player(collision_world);
  player.transform.position = glm::vec3(8.0f, 1.45f, 8.0f);
  player.controller.grounded = false;
  player.locomotion.vertical_velocity = -0.5f;

  InputState input{};
  input.jump_pressed = true;
  PlayerControllerSystem::simulate_fixed(player, input, collision_world,
                                         1.0f / 60.0f, false);
  input.jump_pressed = false;

  for (int i = 0; i < 8; ++i) {
    PlayerControllerSystem::simulate_fixed(player, input, collision_world,
                                           1.0f / 60.0f, false);
  }
  assert(player.locomotion.jump_buffer_timer >= 0.0f);
  assert(player.locomotion.vertical_velocity > 0.0f);
}

void test_player_landing_thresholds() {
  VoxelChunk chunk;
  chunk.generate_flat_ground(0);
  VoxelCollisionWorld collision_world(&chunk);

  InputState input{};
  PlayerEntity soft = PlayerControllerSystem::spawn_player(collision_world);
  soft.transform.position = glm::vec3(8.0f, 1.35f, 8.0f);
  soft.controller.grounded = false;
  soft.locomotion.state = PlayerLocomotionState::AirborneFall;
  soft.locomotion.vertical_velocity = -2.0f;
  for (int i = 0; i < 30 && !soft.controller.grounded; ++i) {
    PlayerControllerSystem::simulate_fixed(soft, input, collision_world,
                                           1.0f / 60.0f, false);
  }
  assert(soft.controller.grounded);
  assert(soft.locomotion.state == PlayerLocomotionState::LandSoft);
  assert(soft.anim_state == PlayerAnimState::LandSoft);

  PlayerEntity hard = PlayerControllerSystem::spawn_player(collision_world);
  hard.transform.position = glm::vec3(8.0f, 4.5f, 8.0f);
  hard.controller.grounded = false;
  hard.locomotion.state = PlayerLocomotionState::AirborneFall;
  hard.locomotion.vertical_velocity = -14.0f;
  for (int i = 0; i < 45 && !hard.controller.grounded; ++i) {
    PlayerControllerSystem::simulate_fixed(hard, input, collision_world,
                                           1.0f / 60.0f, false);
  }
  assert(hard.controller.grounded);
  assert(hard.locomotion.state == PlayerLocomotionState::LandHard);
  assert(hard.anim_state == PlayerAnimState::LandHard);
}

void test_avbd_solver_lifecycle() {
  AvbdSolver solver;
  EnginePhysicsSettings settings{};
  settings.gravity = -9.81f;
  settings.solver_backend = PhysicsSolverBackend::AvbdExperimental;
  solver.init(settings);
  solver.create_minimal_test_scene();
  assert(!solver.vertices().empty());
  assert(!solver.bodies().empty());

  for (int i = 0; i < 180; ++i) {
    solver.step(1.0f / 60.0f);
  }

  bool found_ground_contact = false;
  for (const AvbdVertexState &v : solver.vertices()) {
    assert(std::isfinite(v.position.x));
    assert(std::isfinite(v.position.y));
    assert(std::isfinite(v.position.z));
    assert(std::isfinite(v.velocity.x));
    assert(std::isfinite(v.velocity.y));
    assert(std::isfinite(v.velocity.z));
    if (v.position.y < 0.05f) {
      found_ground_contact = true;
    }
  }
  assert(found_ground_contact);

  solver.shutdown();
  solver.step(1.0f / 60.0f);
}

void test_minigame_snake_runs() {
  MiniGameState state{};
  minigame_begin(state, MiniGameType::Snake, 11u);
  assert(state.active);
  assert(!state.completed);

  InputState input{};
  input.move = glm::vec2(1.0f, 0.0f);
  for (int i = 0; i < 80 && !state.completed; ++i) {
    minigame_tick(state, input, 1.0f / 30.0f);
  }

  assert(state.snake.length >= 3);
}

void test_minigame_golf_shot() {
  MiniGameState state{};
  minigame_begin(state, MiniGameType::Golf, 3u);
  assert(state.active);
  assert(state.golf.strokes == 0);

  InputState input{};
  input.interact_pressed = true;
  minigame_tick(state, input, 1.0f / 60.0f);
  assert(state.golf.strokes == 1);

  input.interact_pressed = false;
  for (int i = 0; i < 120; ++i) {
    minigame_tick(state, input, 1.0f / 60.0f);
  }
  assert(std::isfinite(state.golf.ball.x));
  assert(std::isfinite(state.golf.ball.y));
}

void test_minigame_tetris_progress() {
  MiniGameState state{};
  minigame_begin(state, MiniGameType::Tetris, 17u);
  assert(state.active);
  assert(!state.completed);

  InputState input{};
  for (int i = 0; i < 240 && !state.completed; ++i) {
    input.move.x = (i % 40 < 20) ? -1.0f : 1.0f;
    input.jump_pressed = (i % 23) == 0;
    input.crouch_held = (i % 29) < 4;
    minigame_tick(state, input, 1.0f / 30.0f);
    input.jump_pressed = false;
  }

  assert(state.score >= 0);
}

void test_minigame_racing_completion() {
  MiniGameState state{};
  minigame_begin(state, MiniGameType::Racing, 5u);
  assert(state.active);
  assert(!state.completed);

  InputState input{};
  input.move = glm::vec2(0.0f, 1.0f);
  for (int i = 0; i < 7200 && !state.completed; ++i) {
    minigame_tick(state, input, 1.0f / 60.0f);
  }

  assert(state.racing.lap >= 1);
}

void test_minigame_tictactoe_places_marks() {
  MiniGameState state{};
  minigame_begin(state, MiniGameType::TicTacToe, 9u);
  assert(state.active);

  InputState input{};
  for (int i = 0; i < 6 && !state.completed; ++i) {
    input.interact_pressed = true;
    minigame_tick(state, input, 1.0f / 30.0f);
    input.interact_pressed = false;
    input.move.x = ((i % 2) == 0) ? 1.0f : -1.0f;
    minigame_tick(state, input, 1.0f / 30.0f);
  }

  int occupied = 0;
  for (uint8_t cell : state.tictactoe.board) {
    if (cell != 0) {
      occupied++;
    }
  }
  assert(occupied >= 2);
}

void test_web_platform_state() {
  WebPlatform platform;
  assert(platform.init(640, 360));
  assert(platform.active());
  assert(platform.width() == 640);
  assert(platform.height() == 360);
  assert(platform.poll_events());
  assert(platform.tick() == 1);
  platform.set_focused(false);
  assert(!platform.active());
  assert(!platform.poll_events());
  platform.set_focused(true);
  assert(platform.poll_events());
  platform.shutdown();
  assert(!platform.active());
}

void test_android_platform_state() {
  AndroidPlatform platform;
  assert(platform.init(1920, 1080));
  assert(platform.active());
  assert(platform.width() == 1920);
  assert(platform.height() == 1080);
  assert(platform.poll_events());
  platform.on_pause();
  assert(!platform.active());
  assert(!platform.poll_events());
  platform.on_resume();
  platform.on_resize(1280, 720);
  assert(platform.width() == 1280);
  assert(platform.height() == 720);
  assert(platform.active());
  assert(platform.frame_time_seconds() > 0.0);
  platform.shutdown();
  assert(!platform.active());
}

void test_runtime_session_clamps_large_frame_spike() {
  // After a 10-second frame spike, the 250ms clamp should produce
  // at most ceil(0.25 / (1/60)) == 15 fixed steps (was 600 before clamp).
  RuntimeGameSession session;
  session.reset(1.0 / 60.0);
  uint32_t steps = 0;
  RuntimeGameSessionCallbacks cb;
  cb.simulate_step = [&](const RuntimeGameSessionStepContext &) { ++steps; };
  session.advance(10.0, cb);
  assert(steps <= 15);
  assert(steps > 0);
  // Accumulator should be drained below one fixed_dt
  assert(session.fixed_step().accumulator < session.fixed_step().fixed_dt);
}

void test_vehicle_foundation_fixed_step_counter() {
  FixedStepCounter counter(1.0 / 60.0);
  const uint32_t s0 = counter.consume(1.0 / 30.0);
  assert(s0 == 2);
  const uint32_t s1 = counter.consume(1.0 / 120.0);
  assert(s1 == 0 || s1 == 1);
  assert(counter.tick() >= 2);
  assert(counter.alpha() >= 0.0 && counter.alpha() <= 1.0);
}

void test_vehicle_kinematic_determinism() {
  VehicleKinematicState a{};
  VehicleKinematicState b{};
  const VehicleControlInput input{1.0f, 0.0f, 0.4f, 0.0f};
  const VehiclePhysicsTuning tuning{};
  for (int i = 0; i < 240; ++i) {
    integrate_vehicle_kinematics(a, input, tuning, 1.0f / 60.0f);
    integrate_vehicle_kinematics(b, input, tuning, 1.0f / 60.0f);
  }
  assert(glm::length(a.position - b.position) < 1.0e-4f);
  assert(std::isfinite(a.position.x) && std::isfinite(a.position.y) &&
         std::isfinite(a.position.z));
}

void test_aircraft_kinematic_determinism() {
  AircraftKinematicState a{};
  AircraftKinematicState b{};
  const AircraftControlInput input{0.8f, 0.2f, -0.1f, 0.05f};
  const AircraftPhysicsTuning tuning{};
  for (int i = 0; i < 300; ++i) {
    integrate_aircraft_kinematics(a, input, tuning, 1.0f / 120.0f);
    integrate_aircraft_kinematics(b, input, tuning, 1.0f / 120.0f);
  }
  assert(glm::length(a.position - b.position) < 1.0e-4f);
  assert(std::isfinite(a.euler.x) && std::isfinite(a.euler.y) &&
         std::isfinite(a.euler.z));
}

void test_voxel_physics_bridge_chunk_tracking() {
  VoxelPhysicsBridge bridge;
  VoxelChunk chunk;
  chunk.generate_flat_ground(2);

  const VoxelChunkCoord coord{1, -2};
  assert(!bridge.has_chunk(coord));
  bridge.register_chunk(coord, chunk);
  assert(bridge.has_chunk(coord));
  assert(bridge.chunk_count() == 1);

  assert(bridge.take_chunk_dirty(coord));
  assert(!bridge.take_chunk_dirty(coord));

  bridge.unregister_chunk(coord);
  assert(!bridge.has_chunk(coord));
  assert(bridge.chunk_count() == 0);
}

void test_voxel_physics_bridge_shape_build() {
  VoxelPhysicsBridge bridge;
  VoxelChunk chunk;
  chunk.generate_flat_ground(0);
  bridge.register_chunk(VoxelChunkCoord{2, 3}, chunk);

  const std::vector<VoxelStaticShape> shapes =
      bridge.build_chunk_shapes(VoxelChunkCoord{2, 3});
  assert(!shapes.empty());

  bool found_chunk_offset = false;
  for (const VoxelStaticShape &shape : shapes) {
    assert(shape.max.x > shape.min.x);
    assert(shape.max.y > shape.min.y);
    assert(shape.max.z > shape.min.z);
    if (shape.min.x >= static_cast<float>(2 * VoxelChunk::CHUNK_X) &&
        shape.min.z >= static_cast<float>(3 * VoxelChunk::CHUNK_Z)) {
      found_chunk_offset = true;
    }
  }
  assert(found_chunk_offset);
}

void test_voxel_vehicle_builder_mass_properties() {
  std::vector<VoxelMassCell> cells;
  for (int x = 0; x < 4; ++x) {
    for (int y = 0; y < 2; ++y) {
      for (int z = 0; z < 2; ++z) {
        cells.push_back({glm::ivec3(x, y, z), VoxelMassMaterial{650.0f}});
      }
    }
  }

  const VoxelVehicleBuildResult result =
      build_voxel_vehicle_properties(cells, 0.5f);
  assert(result.mass.total_mass_kg > 0.0f);
  assert(result.mass.center_of_mass.x > 0.5f &&
         result.mass.center_of_mass.x < 1.5f);
  assert(result.mass.center_of_mass.y > 0.2f &&
         result.mass.center_of_mass.y < 0.8f);
  assert(result.mass.inertia_diagonal.x > 0.0f);
  assert(result.mass.inertia_diagonal.y > 0.0f);
  assert(result.mass.inertia_diagonal.z > 0.0f);
  assert(result.wheel_mounts.size() == 4);
}

void test_voxel_vehicle_builder_deterministic() {
  const std::vector<VoxelMassCell> cells{
      {glm::ivec3(0, 0, 0), VoxelMassMaterial{500.0f}},
      {glm::ivec3(1, 0, 0), VoxelMassMaterial{500.0f}},
      {glm::ivec3(0, 1, 0), VoxelMassMaterial{700.0f}},
      {glm::ivec3(1, 1, 0), VoxelMassMaterial{700.0f}}};

  const VoxelVehicleBuildResult a = build_voxel_vehicle_properties(cells, 1.0f);
  const VoxelVehicleBuildResult b = build_voxel_vehicle_properties(cells, 1.0f);
  assert(std::fabs(a.mass.total_mass_kg - b.mass.total_mass_kg) < 1.0e-5f);
  assert(glm::length(a.mass.center_of_mass - b.mass.center_of_mass) < 1.0e-6f);
  assert(glm::length(a.mass.inertia_diagonal - b.mass.inertia_diagonal) <
         1.0e-6f);
}

void test_ground_vehicle_controller_accel_and_brake() {
  VoxelChunk chunk;
  chunk.generate_flat_ground(0);
  VoxelCollisionWorld collision_world(&chunk);

  GroundVehicleController controller;
  controller.reset(glm::vec3(8.0f, 1.4f, 8.0f), 0.0f);

  VehicleControlInput throttle{};
  throttle.throttle = 1.0f;
  uint32_t max_grounded = 0;
  for (int i = 0; i < 60; ++i) {
    controller.step(throttle, collision_world, 1.0f / 60.0f);
    max_grounded =
        std::max(max_grounded, controller.state().telemetry.grounded_wheels);
  }
  const float speed_after_accel = controller.state().telemetry.speed_mps;
  assert(speed_after_accel > 1.5f);
  assert(max_grounded > 0);

  VehicleControlInput brake{};
  brake.brake = 1.0f;
  for (int i = 0; i < 60; ++i) {
    controller.step(brake, collision_world, 1.0f / 60.0f);
  }
  assert(controller.state().telemetry.speed_mps < speed_after_accel);
}

void test_ground_vehicle_forward_direction() {
  VoxelChunk chunk;
  chunk.generate_flat_ground(0);
  VoxelCollisionWorld collision_world(&chunk);

  GroundVehicleController controller;
  controller.reset(glm::vec3(10.0f, 1.2f, 10.0f), 0.0f);

  const float z0 = controller.state().kinematic.position.z;
  VehicleControlInput throttle{};
  throttle.throttle = 1.0f;
  for (int i = 0; i < 60; ++i) {
    controller.step(throttle, collision_world, 1.0f / 60.0f);
  }
  const float z1 = controller.state().kinematic.position.z;
  assert(z1 > z0);
}

void test_aircraft_controller_throttle_and_pitch() {
  VoxelChunk chunk;
  chunk.generate_flat_ground(0);
  VoxelCollisionWorld collision_world(&chunk);

  AircraftController controller;
  controller.reset(glm::vec3(8.0f, 7.0f, 8.0f), glm::vec3(0.0f),
                   glm::vec3(0.0f, 0.0f, 6.0f));

  AircraftControlInput throttle{};
  throttle.throttle = 1.0f;
  for (int i = 0; i < 120; ++i) {
    controller.step(throttle, collision_world, 1.0f / 60.0f);
  }
  const float boosted_speed = controller.state().telemetry.speed_mps;
  assert(boosted_speed > 8.0f);

  AircraftControlInput pitch{};
  pitch.throttle = 0.7f;
  pitch.pitch = 0.4f;
  for (int i = 0; i < 90; ++i) {
    controller.step(pitch, collision_world, 1.0f / 60.0f);
  }
  assert(std::fabs(controller.state().kinematic.euler.x) > 0.01f);
  assert(controller.state().kinematic.position.y > 2.0f);
}

void test_aircraft_controller_yaw_turn_direction() {
  VoxelChunk chunk;
  chunk.generate_flat_ground(0);
  VoxelCollisionWorld collision_world(&chunk);

  AircraftController controller;
  controller.reset(glm::vec3(10.0f, 8.0f, 10.0f), glm::vec3(0.0f),
                   glm::vec3(0.0f, 0.0f, 12.0f));

  AircraftControlInput turn{};
  turn.throttle = 0.75f;
  turn.yaw = 0.55f;
  for (int i = 0; i < 120; ++i) {
    controller.step(turn, collision_world, 1.0f / 60.0f);
  }
  assert(controller.state().kinematic.euler.y > 0.05f);
  assert(controller.state().kinematic.position.x > 10.0f);
}

void test_aircraft_controller_stall_behavior() {
  VoxelChunk chunk;
  chunk.generate_flat_ground(0);
  VoxelCollisionWorld collision_world(&chunk);

  AircraftController controller;
  controller.reset(glm::vec3(14.0f, 12.0f, 14.0f), glm::vec3(0.15f, 0.0f, 0.0f),
                   glm::vec3(0.0f, 0.0f, 4.0f));
  AircraftControlInput idle{};
  idle.throttle = 0.0f;
  for (int i = 0; i < 180; ++i) {
    controller.step(idle, collision_world, 1.0f / 60.0f);
  }
  assert(controller.state().kinematic.position.y < 12.0f);
}

void test_aircraft_controller_idle_damps_horizontal_motion() {
  VoxelChunk chunk;
  chunk.generate_flat_ground(0);
  VoxelCollisionWorld collision_world(&chunk);

  AircraftController controller;
  controller.reset(glm::vec3(14.0f, 10.0f, 14.0f), glm::vec3(0.0f),
                   glm::vec3(8.0f, 0.0f, 0.0f));
  AircraftControlInput idle{};
  idle.throttle = 0.0f;
  const float initial_planar_speed =
      glm::length(glm::vec2(controller.state().kinematic.velocity.x,
                            controller.state().kinematic.velocity.z));
  for (int i = 0; i < 180; ++i) {
    controller.step(idle, collision_world, 1.0f / 60.0f);
  }
  const float final_planar_speed =
      glm::length(glm::vec2(controller.state().kinematic.velocity.x,
                            controller.state().kinematic.velocity.z));
  assert(final_planar_speed < initial_planar_speed * 0.6f);
}

void test_vehicle_drivetrain_shift_behavior() {
  VehicleDrivetrain drivetrain;
  drivetrain.reset();
  float scaled_throttle = 0.0f;

  for (int i = 0; i < 240; ++i) {
    scaled_throttle = drivetrain.update(1.0f, 24.0f, 1.0f / 60.0f);
  }
  assert(drivetrain.telemetry().current_gear >= 2);
  assert(drivetrain.telemetry().engine_rpm > 1500.0f);
  assert(scaled_throttle > 0.1f);
}

void test_vehicle_damage_model_impact() {
  std::vector<VoxelMassCell> cells;
  for (int x = 0; x < 3; ++x) {
    for (int y = 0; y < 2; ++y) {
      for (int z = 0; z < 2; ++z) {
        cells.push_back({glm::ivec3(x, y, z), VoxelMassMaterial{}});
      }
    }
  }

  VehicleDamageModel damage;
  damage.initialize(cells, 100.0f);
  const float integrity_before = damage.stats().integrity;
  damage.apply_impact(glm::vec3(0.5f, 0.5f, 0.5f), 3000.0f, 1.8f);
  const float integrity_after = damage.stats().integrity;
  assert(integrity_after <= integrity_before);
  assert(damage.stats().mass_scale <= 1.0f);
}

void test_vehicle_damage_model_deterministic() {
  const std::vector<VoxelMassCell> cells{
      {glm::ivec3(0, 0, 0), VoxelMassMaterial{}},
      {glm::ivec3(1, 0, 0), VoxelMassMaterial{}},
      {glm::ivec3(0, 1, 0), VoxelMassMaterial{}},
      {glm::ivec3(1, 1, 0), VoxelMassMaterial{}}};

  VehicleDamageModel a;
  VehicleDamageModel b;
  a.initialize(cells, 80.0f);
  b.initialize(cells, 80.0f);

  a.apply_impact(glm::vec3(0.5f, 0.5f, 0.5f), 1000.0f, 1.1f);
  b.apply_impact(glm::vec3(0.5f, 0.5f, 0.5f), 1000.0f, 1.1f);
  assert(std::fabs(a.stats().integrity - b.stats().integrity) < 1.0e-6f);
  assert(a.stats().destroyed_cells == b.stats().destroyed_cells);
}

void test_vehicle_sandbox_scene_step() {
  VehicleSandboxScene sandbox;
  sandbox.init_default();

  InputState vehicle_input{};
  vehicle_input.move = glm::vec2(0.1f, 1.0f);
  InputState aircraft_input{};
  aircraft_input.move = glm::vec2(-0.2f, 0.7f);
  aircraft_input.jump_held = true;

  for (int i = 0; i < 90; ++i) {
    sandbox.step(vehicle_input, aircraft_input, 1.0f / 60.0f);
  }

  const VehicleSandboxSnapshot snap = sandbox.snapshot();
  assert(snap.vehicle_speed_mps > 0.5f);
  assert(snap.aircraft_speed_mps > 6.0f);
  assert(snap.vehicle_position.y > 0.2f);
  assert(snap.aircraft_position.y > 2.0f);
  assert(snap.static_shape_count > 0);
}

} // namespace

int main() {
  test_expedition_mission_requires_ordered_player_actions();
  test_string_id_compile_time_hash();
  test_cvar_register_and_find();
  test_cvar_set_and_get_float();
  test_cvar_readonly_blocked();
  test_cvar_bool();
  test_cvar_command_line_parse();
  test_cvar_not_found_returns_zero();
  test_camera_vectors();
  test_camera_view_override_basis();
  test_coordinate_frames_preserve_body_specific_origins();
  test_surface_orientation_parallel_transports_through_poles();
  test_net_pod_serialization();
  test_session_info_serialization();
  test_chunk_state_serialization();
  test_chunk_runtime_helpers();
  test_planet_face_uv_to_direction_unit_vectors();
  test_planet_direction_to_face();
  test_planet_direction_to_face_uv_roundtrip();
  test_planet_radial_up_and_world_pos();
  test_planet_local_face_world_roundtrip_and_distortion();
  test_planet_tangent_basis_orthonormal();
  test_planet_neighbor_within_face_bounds();
  test_block_world_terrain_height_is_smooth_across_columns();
  test_compact_planet_profile_has_playable_scale();
  test_block_world_production_radius_keeps_meter_scale_detail();
  test_block_world_evicts_chunks_outside_resident_patch();
  test_block_world_stream_includes_all_vertical_rows();
  test_block_world_streamed_surface_meshes_are_non_empty();
  test_player_spawn_on_planet_surface();
  test_player_moves_on_planet_surface();
  test_planet_flight_follows_camera_pitch();
  test_planet_flight_descends_and_lands_on_surface();
  test_block_world_cross_sector_chunk_offset();
  test_block_world_cross_sector_neighbor_lands_on_destination_edge();
  test_solar_system_has_nearby_companion_planet();
  test_cube_edge_pairings_preserve_direction();
  test_surface_height_matches_quantized_surface_block();
  test_grass_mesh_is_deterministic_and_surface_aligned();
  test_planet_quadtree_roots_are_stable();
  test_planet_quadtree_subdivision_child_ids();
  test_planet_terrain_root_chunk_covers_face();
  test_planet_terrain_lod_chunks_cover_expected_regions();
  test_planet_surface_flat_mesh_uses_local_plane();
  test_planet_impostor_is_complete_sphere_lod();
  test_compact_planet_surface_is_complete_and_seamless();
  test_planet_flight_clipmap_is_camera_relative_and_textured();
  test_planet_streamer_returns_runtime_terrain_chunks();
  test_planet_streamer_refines_near_surface();
  test_planet_streamer_reports_budgeted_stats_and_revision();
  test_planet_streamer_reports_evicted_chunks();
  test_atmosphere_transition_manager_descent_and_ascent();
  test_planet_surface_raycast_radial_down_and_up();
  test_net_header_validation();
  test_chunk_meshing();
  test_chunk_world_footprint();
  test_single_voxel_mesh_bounds();
  test_chunk_seed_determinism();
  test_world_generator_hills_and_valleys_are_deterministic_and_bounded();
  test_heightmap_generation_samples_continuously_across_chunk_edges();
  test_chunk_spherical_planet_generation();
  test_voxel_material_surface_assignment();
  test_voxel_material_custom_values_affect_mesh_colors();
  test_camera_yaw_response();
  test_strafe_axis_sign();
  test_player_settles_on_ground();
  test_player_animation_state_transitions();
  test_player_directional_locomotion_states();
  test_player_backward_strafe_preserves_camera_facing();
  test_player_coyote_jump_window();
  test_player_jump_buffer_consumes_on_landing();
  test_player_landing_thresholds();
  test_avbd_solver_lifecycle();
  test_minigame_snake_runs();
  test_minigame_golf_shot();
  test_minigame_tetris_progress();
  test_minigame_racing_completion();
  test_minigame_tictactoe_places_marks();
  test_web_platform_state();
  test_android_platform_state();
  test_runtime_session_clamps_large_frame_spike();
  test_vehicle_foundation_fixed_step_counter();
  test_vehicle_kinematic_determinism();
  test_aircraft_kinematic_determinism();
  test_voxel_physics_bridge_chunk_tracking();
  test_voxel_physics_bridge_shape_build();
  test_voxel_vehicle_builder_mass_properties();
  test_voxel_vehicle_builder_deterministic();
  test_ground_vehicle_controller_accel_and_brake();
  test_ground_vehicle_forward_direction();
  test_aircraft_controller_throttle_and_pitch();
  test_aircraft_controller_yaw_turn_direction();
  test_aircraft_controller_stall_behavior();
  test_aircraft_controller_idle_damps_horizontal_motion();
  test_vehicle_drivetrain_shift_behavior();
  test_vehicle_damage_model_impact();
  test_vehicle_damage_model_deterministic();
  test_vehicle_sandbox_scene_step();
  return 0;
}
