#include "engine_core/cvar.hpp"
#include "engine_core/string_id.hpp"
#include "engine_gameplay/minigames/minigames.hpp"
#include "engine_gameplay/player/player_controller.hpp"
#include "engine_math/camera.hpp"
#include "engine_net_proto/net_protocol_helpers.hpp"
#include "engine_net_proto/net_types.hpp"
#include "engine_world/net_chunk_state.hpp"
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
  RenderMesh mesh = chunk.build_naive_mesh();

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
  const RenderMesh mesh = chunk.build_naive_mesh();
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
  const RenderMesh mesh = chunk.build_naive_mesh();

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

  const RenderMesh mesh_a = a.build_naive_mesh();
  const RenderMesh mesh_b = b.build_naive_mesh();
  const RenderMesh mesh_c = c.build_naive_mesh();

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

void test_chunk_spherical_planet_generation() {
  VoxelChunk chunk;
  chunk.generate_spherical_planet_seeded(0xBEEF1234u);
  const RenderMesh mesh = chunk.build_naive_mesh();
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

void test_locomotion_course_only_affects_origin_chunk() {
  VoxelChunk origin_base;
  VoxelChunk origin_course;
  VoxelChunk adjacent_base;
  VoxelChunk adjacent_course;

  origin_base.generate_heightmap_terrain_seeded(k_voxov_flat_world_seed, 0, 0);
  generate_flat_world_locomotion_chunk(origin_course, k_voxov_flat_world_seed,
                                       0, 0);
  adjacent_base.generate_heightmap_terrain_seeded(k_voxov_flat_world_seed, 1,
                                                  0);
  generate_flat_world_locomotion_chunk(adjacent_course, k_voxov_flat_world_seed,
                                       1, 0);

  int origin_diff = 0;
  int adjacent_diff = 0;
  for (int z = 0; z < VoxelChunk::CHUNK_Z; ++z) {
    for (int y = 0; y < VoxelChunk::CHUNK_Y; ++y) {
      for (int x = 0; x < VoxelChunk::CHUNK_X; ++x) {
        origin_diff +=
            origin_base.solid(x, y, z) != origin_course.solid(x, y, z) ? 1 : 0;
        adjacent_diff +=
            adjacent_base.solid(x, y, z) != adjacent_course.solid(x, y, z) ? 1
                                                                           : 0;
      }
    }
  }

  assert(origin_diff > 0);
  assert(adjacent_diff == 0);
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
  test_string_id_compile_time_hash();
  test_cvar_register_and_find();
  test_cvar_set_and_get_float();
  test_cvar_readonly_blocked();
  test_cvar_bool();
  test_cvar_command_line_parse();
  test_cvar_not_found_returns_zero();
  test_camera_vectors();
  test_camera_view_override_basis();
  test_net_pod_serialization();
  test_session_info_serialization();
  test_chunk_state_serialization();
  test_chunk_runtime_helpers();
  test_net_header_validation();
  test_chunk_meshing();
  test_chunk_world_footprint();
  test_single_voxel_mesh_bounds();
  test_chunk_seed_determinism();
  test_chunk_spherical_planet_generation();
  test_locomotion_course_only_affects_origin_chunk();
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
