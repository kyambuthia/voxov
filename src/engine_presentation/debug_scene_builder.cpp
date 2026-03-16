#include "engine_presentation/debug_scene_builder.hpp"

#include "engine_gameplay/animation/skeletal_animator.hpp"
#include "engine_gameplay/player/player_visuals.hpp"
#include "engine_render/debug_draw/debug_draw.hpp"

#include <algorithm>
#include <cmath>

namespace {
constexpr float k_vehicle_body_half_length = 1.35f;
constexpr float k_vehicle_body_half_width = 0.8f;
constexpr float k_vehicle_body_height = 0.65f;
constexpr bool k_vehicle_feature_enabled = false;
constexpr float k_vehicle_visual_yaw_offset = 0.0f;
constexpr float k_aircraft_body_length = 2.7f;
constexpr float k_aircraft_body_width = 1.1f;
constexpr float k_aircraft_body_height = 0.55f;

glm::vec3 rotate_y(const glm::vec3 &v, float yaw_radians) {
  const float c = std::cos(yaw_radians);
  const float s = std::sin(yaw_radians);
  return glm::vec3(v.x * c - v.z * s, v.y, v.x * s + v.z * c);
}

struct SurfaceFrame {
  glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
  glm::vec3 east = glm::vec3(1.0f, 0.0f, 0.0f);
  glm::vec3 north = glm::vec3(0.0f, 0.0f, 1.0f);
};

SurfaceFrame make_surface_frame(glm::vec3 up_raw) {
  SurfaceFrame frame{};
  if (glm::length(up_raw) > 0.001f) {
    frame.up = glm::normalize(up_raw);
  }
  glm::vec3 ref_axis(0.0f, 1.0f, 0.0f);
  if (std::fabs(glm::dot(frame.up, ref_axis)) > 0.94f) {
    ref_axis = glm::vec3(1.0f, 0.0f, 0.0f);
  }
  frame.east = glm::normalize(glm::cross(ref_axis, frame.up));
  frame.north = glm::normalize(glm::cross(frame.up, frame.east));
  return frame;
}

glm::vec3 rotate_on_surface(const glm::vec3 &local, const SurfaceFrame &frame,
                            float yaw_radians) {
  glm::vec3 forward =
      frame.north * std::cos(yaw_radians) + frame.east * std::sin(yaw_radians);
  if (glm::length(forward) <= 0.001f) {
    forward = frame.north;
  } else {
    forward = glm::normalize(forward);
  }
  glm::vec3 right = glm::cross(forward, frame.up);
  if (glm::length(right) <= 0.001f) {
    right = frame.east;
  } else {
    right = glm::normalize(right);
  }
  return right * local.x + frame.up * local.y + forward * local.z;
}

glm::vec3 player_color_from_id(uint32_t player_id) {
  return player_color_from_network_id(player_id);
}

glm::vec3 minigame_color(MiniGameType type) {
  switch (type) {
  case MiniGameType::Snake:
    return glm::vec3(0.15f, 0.85f, 0.25f);
  case MiniGameType::Golf:
    return glm::vec3(0.20f, 0.72f, 0.95f);
  case MiniGameType::Tetris:
    return glm::vec3(0.86f, 0.42f, 0.92f);
  case MiniGameType::Racing:
    return glm::vec3(1.0f, 0.55f, 0.20f);
  case MiniGameType::TicTacToe:
    return glm::vec3(0.95f, 0.95f, 0.32f);
  default:
    return glm::vec3(0.8f, 0.8f, 0.8f);
  }
}

glm::ivec2 tetris_visual_cell(int shape, int rot, int i) {
  static constexpr glm::ivec2 k_shape_rot[4][4][4] = {
      {
          {{-1, 0}, {0, 0}, {1, 0}, {2, 0}},
          {{0, -1}, {0, 0}, {0, 1}, {0, 2}},
          {{-1, 1}, {0, 1}, {1, 1}, {2, 1}},
          {{1, -1}, {1, 0}, {1, 1}, {1, 2}},
      },
      {
          {{0, 0}, {1, 0}, {0, 1}, {1, 1}},
          {{0, 0}, {1, 0}, {0, 1}, {1, 1}},
          {{0, 0}, {1, 0}, {0, 1}, {1, 1}},
          {{0, 0}, {1, 0}, {0, 1}, {1, 1}},
      },
      {
          {{-1, 0}, {0, 0}, {1, 0}, {0, 1}},
          {{0, -1}, {0, 0}, {0, 1}, {1, 0}},
          {{-1, 0}, {0, 0}, {1, 0}, {0, -1}},
          {{0, -1}, {0, 0}, {0, 1}, {-1, 0}},
      },
      {
          {{-1, 0}, {0, 0}, {1, 0}, {1, 1}},
          {{0, -1}, {0, 0}, {0, 1}, {1, -1}},
          {{-1, -1}, {-1, 0}, {0, 0}, {1, 0}},
          {{-1, 1}, {0, -1}, {0, 0}, {0, 1}},
      }};
  return k_shape_rot[shape % 4][rot % 4][i % 4];
}

struct AnimatedCapsuleShape {
  float radius = 0.35f;
  float height = 1.8f;
  float bob = 0.0f;
  float pivot_height = 1.5f;
};

float anim_pulse(float phase) { return std::fabs(std::sin(phase)); }

AnimatedCapsuleShape animated_shape(uint8_t anim_state, float anim_phase,
                                    float anim_blend, float base_radius,
                                    float base_height,
                                    float base_pivot_height) {
  AnimatedCapsuleShape out{};
  out.radius = base_radius;
  out.height = base_height;
  out.pivot_height = base_pivot_height;
  out.bob = 0.01f * std::sin(anim_phase);

  switch (anim_state) {
  case static_cast<uint8_t>(PlayerAnimState::StartMove):
  case static_cast<uint8_t>(PlayerAnimState::LocomotionWalk):
  case static_cast<uint8_t>(PlayerAnimState::PivotLeft):
  case static_cast<uint8_t>(PlayerAnimState::PivotRight):
  case static_cast<uint8_t>(PlayerAnimState::TurnInPlaceLeft):
  case static_cast<uint8_t>(PlayerAnimState::TurnInPlaceRight):
  case static_cast<uint8_t>(PlayerAnimState::MovingTurn):
    out.bob = 0.06f * std::max(0.35f, anim_blend) * anim_pulse(anim_phase);
    break;
  case static_cast<uint8_t>(PlayerAnimState::LocomotionRun):
    out.bob = 0.11f * std::max(0.55f, anim_blend) * anim_pulse(anim_phase);
    break;
  case static_cast<uint8_t>(PlayerAnimState::JumpTakeoff):
  case static_cast<uint8_t>(PlayerAnimState::JumpLoop):
  case static_cast<uint8_t>(PlayerAnimState::FallLoop):
  case static_cast<uint8_t>(PlayerAnimState::LandSoft):
  case static_cast<uint8_t>(PlayerAnimState::LandHard):
    out.bob = 0.08f * std::sin(anim_phase * 0.65f);
    break;
  case static_cast<uint8_t>(PlayerAnimState::StopMove):
  case static_cast<uint8_t>(PlayerAnimState::Recovery):
    out.height = base_height * 0.55f;
    out.radius = base_radius * 1.08f;
    out.pivot_height = base_pivot_height * 0.62f;
    out.bob = 0.02f * anim_pulse(anim_phase * 0.8f);
    break;
  case static_cast<uint8_t>(PlayerAnimState::Idle):
  default:
    break;
  }

  return out;
}

void append_vehicle_tri(RenderMesh &mesh, const glm::vec3 &a, const glm::vec3 &b,
                        const glm::vec3 &c, const glm::vec3 &color) {
  const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
  mesh.vertices.push_back({a, color});
  mesh.vertices.push_back({b, color});
  mesh.vertices.push_back({c, color});
  mesh.indices.push_back(base + 0);
  mesh.indices.push_back(base + 1);
  mesh.indices.push_back(base + 2);
}

void append_vehicle_quad(RenderMesh &mesh, const glm::vec3 &a,
                         const glm::vec3 &b, const glm::vec3 &c,
                         const glm::vec3 &d, const glm::vec3 &color) {
  append_vehicle_tri(mesh, a, b, c, color);
  append_vehicle_tri(mesh, a, c, d, color);
}

void append_vehicle_box(RenderMesh &mesh,
                        const RuntimeVehicleDebugSnapshot &vehicle,
                        const glm::vec3 &local_center,
                        const glm::vec3 &half_extent,
                        const glm::vec3 &color) {
  const float visual_yaw = vehicle.yaw + k_vehicle_visual_yaw_offset;
  const glm::vec3 lc[8] = {
      {-half_extent.x, -half_extent.y, -half_extent.z},
      {half_extent.x, -half_extent.y, -half_extent.z},
      {-half_extent.x, half_extent.y, -half_extent.z},
      {half_extent.x, half_extent.y, -half_extent.z},
      {-half_extent.x, -half_extent.y, half_extent.z},
      {half_extent.x, -half_extent.y, half_extent.z},
      {-half_extent.x, half_extent.y, half_extent.z},
      {half_extent.x, half_extent.y, half_extent.z},
  };
  glm::vec3 p[8]{};
  for (int i = 0; i < 8; ++i) {
    p[i] = vehicle.position + rotate_y(local_center + lc[i], visual_yaw);
  }
  append_vehicle_quad(mesh, p[0], p[1], p[3], p[2], color);
  append_vehicle_quad(mesh, p[4], p[6], p[7], p[5], color);
  append_vehicle_quad(mesh, p[0], p[2], p[6], p[4], color);
  append_vehicle_quad(mesh, p[1], p[5], p[7], p[3], color);
  append_vehicle_quad(mesh, p[2], p[3], p[7], p[6], color);
  append_vehicle_quad(mesh, p[0], p[4], p[5], p[1], color);
}

void append_board_voxel(RenderMesh &mesh, const glm::vec3 &center,
                        const glm::vec3 &half, const glm::vec3 &color) {
  append_mesh(mesh, build_debug_aabb_mesh(center - half, center + half, color));
}

void append_vehicle_and_aircraft_debug(
    RenderMesh &mesh, const RuntimeDebugSceneSnapshot &snapshot);
void append_active_minigame_debug(RenderMesh &mesh,
                                  const RuntimeDebugSceneSnapshot &snapshot,
                                  const PlayerEntity &local_player);
void append_world_feature_debug(RenderMesh &mesh,
                                const RuntimeDebugSceneSnapshot &snapshot,
                                const PlayerEntity &local_player);
void append_player_debug(RenderMesh &mesh, const PlayerEntity &player,
                         const PlayerAnimationRuntime &animation,
                         const SkinnedModel *selected_player_model,
                         bool render_skinned_avatar,
                         bool render_skeleton_only,
                         bool collision_debug_enabled, bool devhud_enabled,
                         const glm::vec3 &target_color,
                         const glm::vec3 &skeleton_color,
                         float skeleton_thickness,
                         float target_radius);
void append_collision_debug(RenderMesh &mesh,
                            const RuntimeDebugSceneSnapshot &snapshot,
                            const PlayerEntity &local_player);
void append_remote_player_debug(RenderMesh &mesh,
                                const RuntimeDebugSceneSnapshot &snapshot,
                                const PlayerEntity &local_player,
                                const SkinnedModel *selected_player_model,
                                bool render_skinned_avatar);

void append_vehicle_and_aircraft_debug(
    RenderMesh &mesh, const RuntimeDebugSceneSnapshot &snapshot) {
  if (!k_vehicle_feature_enabled || snapshot.debug_collision_only ||
      snapshot.vehicle.controller == nullptr) {
    return;
  }

  const RuntimeVehicleDebugSnapshot &vehicle = snapshot.vehicle;
  append_vehicle_box(
      mesh, vehicle, glm::vec3(0.0f, k_vehicle_body_height * 0.5f, 0.0f),
      glm::vec3(k_vehicle_body_half_width, k_vehicle_body_height * 0.5f,
                k_vehicle_body_half_length),
      vehicle.occupied ? glm::vec3(0.15f, 0.78f, 0.35f)
                       : glm::vec3(0.85f, 0.62f, 0.22f));
  append_vehicle_box(mesh, vehicle,
                     glm::vec3(0.0f, k_vehicle_body_height + 0.28f, -0.1f),
                     glm::vec3(0.58f, 0.28f, 0.68f),
                     glm::vec3(0.2f, 0.35f, 0.42f));
  append_vehicle_box(mesh, vehicle,
                     glm::vec3(0.0f, 0.58f, k_vehicle_body_half_length - 0.22f),
                     glm::vec3(0.55f, 0.12f, 0.14f),
                     glm::vec3(0.08f, 0.08f, 0.08f));

  const auto &wheel_setup = snapshot.vehicle.controller->wheel_setup();
  const float visual_yaw = vehicle.yaw + k_vehicle_visual_yaw_offset;
  const auto &wheel_compression =
      snapshot.vehicle.controller->state().wheel_compression;
  for (size_t i = 0; i < wheel_setup.size(); ++i) {
    const GroundVehicleWheel &wheel = wheel_setup[i];
    const float suspension_length =
        wheel.suspension_rest_length -
        wheel_compression[i] * wheel.suspension_travel;
    const glm::vec3 wheel_offset(wheel.local_mount.x,
                                 wheel.local_mount.y - suspension_length,
                                 wheel.local_mount.z);
    append_mesh(mesh,
                build_debug_sphere_mesh(
                    vehicle.position + rotate_y(wheel_offset, visual_yaw),
                    wheel.radius, glm::vec3(0.12f, 0.12f, 0.12f)));
  }

  append_mesh(mesh,
              build_debug_line_mesh(vehicle.position + glm::vec3(0.0f, 0.2f, 0.0f),
                                    vehicle.position + glm::vec3(0.0f, 4.2f, 0.0f),
                                    0.06f, glm::vec3(1.0f, 0.25f, 0.9f)));
  append_mesh(mesh,
              build_debug_sphere_mesh(vehicle.position + glm::vec3(0.0f, 4.35f, 0.0f),
                                      0.22f, glm::vec3(1.0f, 0.25f, 0.9f)));

  const RuntimeAircraftDebugSnapshot &aircraft = snapshot.aircraft;
  const glm::vec3 aircraft_color = aircraft.occupied
                                       ? glm::vec3(0.12f, 0.84f, 0.95f)
                                       : glm::vec3(0.42f, 0.70f, 0.95f);
  auto append_aircraft_box = [&](const glm::vec3 &local_center,
                                 const glm::vec3 &half_extent) {
    const glm::vec3 lc[8] = {
        {-half_extent.x, -half_extent.y, -half_extent.z},
        {half_extent.x, -half_extent.y, -half_extent.z},
        {-half_extent.x, half_extent.y, -half_extent.z},
        {half_extent.x, half_extent.y, -half_extent.z},
        {-half_extent.x, -half_extent.y, half_extent.z},
        {half_extent.x, -half_extent.y, half_extent.z},
        {-half_extent.x, half_extent.y, half_extent.z},
        {half_extent.x, half_extent.y, half_extent.z},
    };
    glm::vec3 p[8]{};
    for (int i = 0; i < 8; ++i) {
      p[i] = aircraft.position + rotate_y(local_center + lc[i], aircraft.yaw);
    }
    append_vehicle_quad(mesh, p[0], p[1], p[3], p[2], aircraft_color);
    append_vehicle_quad(mesh, p[4], p[6], p[7], p[5], aircraft_color);
    append_vehicle_quad(mesh, p[0], p[2], p[6], p[4], aircraft_color);
    append_vehicle_quad(mesh, p[1], p[5], p[7], p[3], aircraft_color);
    append_vehicle_quad(mesh, p[2], p[3], p[7], p[6], aircraft_color);
    append_vehicle_quad(mesh, p[0], p[4], p[5], p[1], aircraft_color);
  };

  append_aircraft_box(glm::vec3(0.0f, k_aircraft_body_height, 0.0f),
                      glm::vec3(k_aircraft_body_width * 0.5f,
                                k_aircraft_body_height,
                                k_aircraft_body_length * 0.5f));
  append_aircraft_box(glm::vec3(0.0f, k_aircraft_body_height + 0.1f, -0.2f),
                      glm::vec3(2.1f, 0.08f, 0.36f));
  append_aircraft_box(glm::vec3(0.0f, k_aircraft_body_height + 0.5f, -1.0f),
                      glm::vec3(0.16f, 0.44f, 0.16f));

  append_mesh(mesh,
              build_debug_line_mesh(
                  aircraft.position,
                  aircraft.position +
                      rotate_y(glm::vec3(0.0f, 0.0f, 4.0f), aircraft.yaw),
                  0.05f, glm::vec3(0.2f, 0.9f, 1.0f)));
}

void append_active_minigame_debug(RenderMesh &mesh,
                                  const RuntimeDebugSceneSnapshot &snapshot,
                                  const PlayerEntity &local_player) {
  const MiniGameState &active_minigame = snapshot.active_minigame;
  if (!active_minigame.active || snapshot.active_minigame_hotspot < 0 ||
      snapshot.active_minigame_hotspot >=
          static_cast<int>(snapshot.minigame_hotspots.size())) {
    return;
  }

  const float board_yaw =
      local_player.camera_rig.yaw * 0.01745329251994329577f;
  glm::vec3 board_origin =
      local_player.transform.position +
      rotate_y(glm::vec3(0.0f, 1.28f, 2.35f), board_yaw);
  SurfaceFrame board_frame{};
  bool use_surface_frame = false;
  if (snapshot.spherical_planet && snapshot.spherical_planet_radius > 0.0f) {
    const glm::vec3 up =
        local_player.transform.position - snapshot.spherical_planet_center;
    board_frame = make_surface_frame(up);
    board_origin = local_player.transform.position +
                   rotate_on_surface(glm::vec3(0.0f, 1.28f, 2.35f), board_frame,
                                     board_yaw);
    use_surface_frame = true;
  }

  auto board_point = [&](const glm::vec3 &local) {
    if (use_surface_frame) {
      return board_origin + rotate_on_surface(local, board_frame, board_yaw);
    }
    return board_origin + rotate_y(local, board_yaw);
  };
  auto append_local_board_voxel = [&](const glm::vec3 &local_center,
                                      const glm::vec3 &half,
                                      const glm::vec3 &color) {
    append_board_voxel(mesh, board_point(local_center), half, color);
  };

  append_local_board_voxel(glm::vec3(0.0f, -0.20f, 0.0f),
                           glm::vec3(1.20f, 0.07f, 0.92f),
                           glm::vec3(0.18f, 0.20f, 0.23f));
  append_local_board_voxel(glm::vec3(0.0f, -0.08f, 0.0f),
                           glm::vec3(1.14f, 0.03f, 0.84f),
                           glm::vec3(0.10f, 0.12f, 0.15f));
  append_local_board_voxel(glm::vec3(0.0f, 0.72f, -0.78f),
                           glm::vec3(1.16f, 0.78f, 0.05f),
                           glm::vec3(0.09f, 0.10f, 0.13f));

  if (active_minigame.type == MiniGameType::Snake) {
    const float cell = 0.18f;
    const glm::vec3 base(-0.80f, -0.12f, -0.70f);
    const glm::vec3 half(0.075f, 0.075f, 0.055f);
    for (int i = 0; i < active_minigame.snake.length; ++i) {
      const glm::ivec2 c = active_minigame.snake.body[static_cast<size_t>(i)];
      append_local_board_voxel(base + glm::vec3(c.x * cell, c.y * cell, 0.0f),
                               half, glm::vec3(0.2f, 0.9f, 0.3f));
    }
    append_local_board_voxel(
        base + glm::vec3(active_minigame.snake.food.x * cell,
                         active_minigame.snake.food.y * cell, 0.0f),
        half, glm::vec3(0.95f, 0.25f, 0.2f));
    return;
  }

  if (active_minigame.type == MiniGameType::TicTacToe) {
    const glm::vec3 base(-0.32f, 0.01f, -0.32f);
    const int cursor = std::clamp(active_minigame.tictactoe.cursor, 0, 8);
    for (int y = 0; y < 3; ++y) {
      for (int x = 0; x < 3; ++x) {
        const int idx = y * 3 + x;
        const uint8_t cell =
            active_minigame.tictactoe.board[static_cast<size_t>(idx)];
        const glm::vec3 cpos = base + glm::vec3(x * 0.32f, 0.0f, y * 0.32f);
        append_local_board_voxel(cpos, glm::vec3(0.11f, 0.03f, 0.11f),
                                 glm::vec3(0.18f, 0.22f, 0.26f));
        if (!active_minigame.completed && idx == cursor) {
          append_local_board_voxel(cpos + glm::vec3(0.0f, 0.055f, 0.0f),
                                   glm::vec3(0.11f, 0.015f, 0.11f),
                                   glm::vec3(0.95f, 0.95f, 0.35f));
        }
        if (cell == 1) {
          append_local_board_voxel(cpos + glm::vec3(0.0f, 0.1f, 0.0f),
                                   glm::vec3(0.05f),
                                   glm::vec3(0.15f, 0.9f, 0.3f));
        } else if (cell == 2) {
          append_local_board_voxel(cpos + glm::vec3(0.0f, 0.1f, 0.0f),
                                   glm::vec3(0.05f),
                                   glm::vec3(0.9f, 0.2f, 0.2f));
        }
      }
    }
    return;
  }

  if (active_minigame.type == MiniGameType::Golf) {
    const glm::vec3 base(-0.78f, 0.0f, -0.55f);
    const glm::vec3 half(0.07f, 0.07f, 0.07f);
    const glm::vec3 ball =
        base + glm::vec3(active_minigame.golf.ball.x * 0.16f, 0.0f,
                         active_minigame.golf.ball.y * 0.16f);
    const glm::vec3 hole =
        base + glm::vec3(active_minigame.golf.hole.x * 0.16f, 0.0f,
                         active_minigame.golf.hole.y * 0.16f);
    append_local_board_voxel(ball, half, glm::vec3(0.9f));
    append_local_board_voxel(hole, half, glm::vec3(0.2f, 0.6f, 1.0f));

    const glm::vec3 aim_dir(std::cos(active_minigame.golf.aim_radians), 0.0f,
                            std::sin(active_minigame.golf.aim_radians));
    const float aim_len = 0.35f + active_minigame.golf.power * 0.65f;
    append_mesh(mesh,
                build_debug_line_mesh(
                    board_point(ball + glm::vec3(0.0f, 0.08f, 0.0f)),
                    board_point(ball + glm::vec3(0.0f, 0.08f, 0.0f) +
                                aim_dir * aim_len),
                    0.03f, glm::vec3(1.0f, 0.9f, 0.3f)));

    const glm::vec3 power_anchor = glm::vec3(-0.90f, 0.02f, -0.68f);
    append_local_board_voxel(power_anchor, glm::vec3(0.12f, 0.02f, 0.02f),
                             glm::vec3(0.2f, 0.2f, 0.24f));
    append_local_board_voxel(
        power_anchor +
            glm::vec3((-0.12f + active_minigame.golf.power * 0.24f), 0.03f,
                      0.0f),
        glm::vec3(std::max(0.02f, active_minigame.golf.power * 0.12f), 0.015f,
                  0.015f),
        glm::vec3(0.2f + active_minigame.golf.power * 0.8f, 0.7f, 0.25f));
    return;
  }

  if (active_minigame.type == MiniGameType::Tetris) {
    const float cell_size = 0.18f;
    const float row_height = 0.095f;
    const glm::vec3 base(-0.90f, -0.46f, -0.68f);
    for (int y = 0; y < TetrisState::k_board_h; ++y) {
      for (int x = 0; x < TetrisState::k_board_w; ++x) {
        const uint8_t filled = active_minigame.tetris.board[static_cast<size_t>(
            y * TetrisState::k_board_w + x)];
        if (filled == 0) {
          continue;
        }
        append_local_board_voxel(
            base + glm::vec3(x * cell_size, y * row_height, 0.0f),
            glm::vec3(0.075f, 0.040f, 0.045f),
            glm::vec3(0.78f, 0.42f, 0.92f));
      }
    }
    if (!active_minigame.completed) {
      for (int i = 0; i < 4; ++i) {
        const glm::ivec2 c =
            tetris_visual_cell(active_minigame.tetris.active_shape,
                               active_minigame.tetris.active_rotation, i);
        const int x = active_minigame.tetris.active_x + c.x;
        const int y = active_minigame.tetris.active_y + c.y;
        if (x < 0 || x >= TetrisState::k_board_w || y < 0 ||
            y >= TetrisState::k_board_h) {
          continue;
        }
        append_local_board_voxel(
            base + glm::vec3(x * cell_size, y * row_height + 0.02f, 0.0f),
            glm::vec3(0.075f, 0.045f, 0.050f),
            glm::vec3(0.95f, 0.75f, 0.25f));
      }
    }
    return;
  }

  if (active_minigame.type == MiniGameType::Racing) {
    constexpr float track_len = 1.8f;
    constexpr float track_half_w = 0.16f;
    const glm::vec3 base(-0.86f, 0.0f, 0.0f);
    append_local_board_voxel(base + glm::vec3(track_len * 0.5f, 0.0f, 0.0f),
                             glm::vec3(track_len * 0.5f, 0.02f, track_half_w),
                             glm::vec3(0.22f, 0.22f, 0.24f));

    for (int cp = 0; cp < 4; ++cp) {
      const float t = static_cast<float>(cp) / 4.0f;
      const float x = t * track_len;
      append_local_board_voxel(base + glm::vec3(x, 0.05f, -track_half_w),
                               glm::vec3(0.02f, 0.06f, 0.02f),
                               glm::vec3(0.95f, 0.45f, 0.2f));
      append_local_board_voxel(base + glm::vec3(x, 0.05f, track_half_w),
                               glm::vec3(0.02f, 0.06f, 0.02f),
                               glm::vec3(0.95f, 0.45f, 0.2f));
    }

    const float progress =
        std::clamp(active_minigame.racing.track_progress / 65.0f, 0.0f, 1.0f);
    append_local_board_voxel(base + glm::vec3(progress * track_len, 0.05f, 0.0f),
                             glm::vec3(0.07f, 0.07f, 0.09f),
                             glm::vec3(1.0f, 0.75f, 0.2f));
  }
}

void append_world_feature_debug(RenderMesh &mesh,
                                const RuntimeDebugSceneSnapshot &snapshot,
                                const PlayerEntity &local_player) {
  if (snapshot.debug_collision_only) {
    return;
  }

  for (const auto &hotspot : snapshot.minigame_hotspots) {
    const glm::vec3 color = minigame_color(hotspot.type);
    const bool selected = hotspot.nearby || hotspot.active;
    glm::vec3 up(0.0f, 1.0f, 0.0f);
    if (snapshot.spherical_planet && snapshot.spherical_planet_radius > 0.0f) {
      up = glm::normalize(hotspot.position - snapshot.spherical_planet_center);
    }
    append_mesh(mesh, build_debug_line_mesh(hotspot.position + up * 0.2f,
                                            hotspot.position + up * 3.0f,
                                            selected ? 0.09f : 0.06f, color));
    append_mesh(mesh,
                build_debug_sphere_mesh(hotspot.position + up * 3.2f,
                                        selected ? 0.28f : 0.2f, color));
    append_mesh(mesh,
                build_debug_sphere_mesh(hotspot.position + up * 0.15f,
                                        selected ? 0.17f : 0.12f,
                                        color * glm::vec3(1.1f)));
  }

  for (const auto &node : snapshot.objective_nodes) {
    const bool selected = node.nearby;
    const glm::vec3 color = node.activated ? glm::vec3(0.18f, 0.82f, 0.36f)
                                           : glm::vec3(0.95f, 0.72f, 0.24f);
    append_mesh(mesh,
                build_debug_line_mesh(node.position + glm::vec3(0.0f, 0.1f, 0.0f),
                                      node.position + glm::vec3(0.0f, 2.6f, 0.0f),
                                      selected ? 0.08f : 0.05f, color));
    append_mesh(mesh,
                build_debug_sphere_mesh(node.position + glm::vec3(0.0f, 2.9f, 0.0f),
                                        selected ? 0.30f : 0.22f, color));
  }

  if (snapshot.extraction_unlocked || snapshot.objective_round_complete) {
    const glm::vec3 extraction_color = snapshot.objective_round_complete
                                           ? glm::vec3(0.22f, 0.95f, 0.48f)
                                           : glm::vec3(0.24f, 0.72f, 0.98f);
    append_mesh(mesh,
                build_debug_sphere_mesh(snapshot.extraction_zone_position,
                                        snapshot.extraction_zone_radius * 0.42f,
                                        extraction_color));
    append_mesh(mesh,
                build_debug_line_mesh(snapshot.extraction_zone_position,
                                      snapshot.extraction_zone_position +
                                          glm::vec3(0.0f, 3.0f, 0.0f),
                                      0.06f, extraction_color));
  }

  append_active_minigame_debug(mesh, snapshot, local_player);
}

void append_player_debug(RenderMesh &mesh, const PlayerEntity &player,
                         const PlayerAnimationRuntime &animation,
                         const SkinnedModel *selected_player_model,
                         bool render_skinned_avatar,
                         bool render_skeleton_only,
                         bool collision_debug_enabled, bool devhud_enabled,
                         const glm::vec3 &target_color,
                         const glm::vec3 &skeleton_color,
                         float skeleton_thickness,
                         float target_radius) {
  const AnimatedCapsuleShape shape = animated_shape(
      static_cast<uint8_t>(player.anim_state), player.anim_phase,
      player.anim_blend, player.controller.capsuleRadius,
      player.controller.capsuleHeight, player.camera_rig.pivotHeight);
  const RenderMesh player_capsule = build_debug_capsule_mesh(
      player.transform.position, shape.radius, shape.height,
      player_color_from_id(player.network_id));
  const RenderMesh target_marker = build_debug_sphere_mesh(
      player.transform.position + glm::vec3(0.0f, shape.pivot_height, 0.0f),
      target_radius, target_color);

  if ((!render_skinned_avatar && !render_skeleton_only) || collision_debug_enabled ||
      devhud_enabled) {
    append_mesh(mesh, player_capsule);
    append_mesh(mesh, target_marker);
  }
  if (render_skinned_avatar && selected_player_model != nullptr) {
    append_mesh(mesh, selected_player_model->build_render_mesh(
                          animation, player.transform.position,
                          player.transform.rotation,
                          player_color_from_id(player.network_id)));
  }
  if (render_skeleton_only ||
      (render_skinned_avatar && (collision_debug_enabled || devhud_enabled))) {
    if (render_skinned_avatar && selected_player_model != nullptr) {
      selected_player_model->append_debug_skeleton(
          mesh, animation, player.transform.position, player.transform.rotation,
          skeleton_color, skeleton_thickness);
    } else {
      const SkeletonPose pose = SkeletalAnimator::sample_pose(
          player.anim_state, player.anim_phase, player.anim_blend);
      SkeletalAnimator::append_debug_skeleton(
          mesh, pose, player.transform.position, player.transform.rotation,
          skeleton_color, skeleton_thickness);
    }
  }
}

void append_collision_debug(RenderMesh &mesh,
                            const RuntimeDebugSceneSnapshot &snapshot,
                            const PlayerEntity &local_player) {
  if (!snapshot.collision_debug_enabled ||
      !(snapshot.devhud_enabled || snapshot.debug_collision_only)) {
    return;
  }

  for (const glm::ivec3 &cell : snapshot.last_collision_debug.overlapped_voxels) {
    const glm::vec3 bmin(static_cast<float>(cell.x), static_cast<float>(cell.y),
                         static_cast<float>(cell.z));
    const glm::vec3 bmax = bmin + glm::vec3(1.0f);
    append_mesh(mesh,
                build_debug_aabb_mesh(bmin, bmax,
                                      glm::vec3(0.95f, 0.15f, 0.15f)));
  }

  append_mesh(mesh, build_debug_line_mesh(
                        snapshot.last_collision_debug.grounding_ray_origin,
                        snapshot.last_collision_debug.grounding_ray_hit, 0.01f,
                        glm::vec3(1.0f, 1.0f, 0.2f)));
  append_mesh(mesh,
              build_debug_line_mesh(
                  local_player.transform.position + glm::vec3(0.0f, 0.05f, 0.0f),
                  local_player.transform.position + glm::vec3(0.0f, 0.05f, 0.0f) +
                      snapshot.last_collision_debug.contact_normal * 0.6f,
                  0.01f, glm::vec3(1.0f, 0.4f, 0.1f)));
}

void append_remote_player_debug(RenderMesh &mesh,
                                const RuntimeDebugSceneSnapshot &snapshot,
                                const PlayerEntity &local_player,
                                const SkinnedModel *selected_player_model,
                                bool render_skinned_avatar) {
  if (snapshot.debug_collision_only || snapshot.collision_world == nullptr) {
    return;
  }

  const VoxelCollisionWorld &collision_world = *snapshot.collision_world;
  for (const auto &render_player : snapshot.remote_players) {
    const float remote_y =
        std::isfinite(render_player.position.y)
            ? render_player.position.y
            : collision_world.find_spawn_height(
                  glm::vec2(render_player.position.x, render_player.position.z),
                  local_player.controller.capsuleRadius,
                  local_player.controller.capsuleHeight) +
                  0.05f;
    const AnimatedCapsuleShape remote_shape = animated_shape(
        render_player.anim_state, render_player.anim_phase,
        render_player.anim_blend, local_player.controller.capsuleRadius,
        local_player.controller.capsuleHeight, local_player.camera_rig.pivotHeight);
    const glm::vec3 remote_base(render_player.position.x, remote_y,
                                render_player.position.z);
    if (!render_skinned_avatar || snapshot.collision_debug_enabled ||
        snapshot.devhud_enabled) {
      append_mesh(mesh,
                  build_debug_capsule_mesh(
                      remote_base, remote_shape.radius, remote_shape.height,
                      player_color_from_id(render_player.player_id)));
    }
    if (render_skinned_avatar && selected_player_model != nullptr &&
        render_player.animation_runtime != nullptr) {
      append_mesh(
          mesh, selected_player_model->build_render_mesh(
                    *render_player.animation_runtime, remote_base,
                    render_player.orientation,
                    player_color_from_id(render_player.player_id) *
                        glm::vec3(1.08f, 1.08f, 1.08f)));
    }
  }
}
} // namespace

RenderMesh DebugSceneBuilder::build(
    const RuntimeDebugSceneSnapshot &snapshot) const {
  if (snapshot.local_player == nullptr || snapshot.local_player_animation == nullptr) {
    return RenderMesh{};
  }

  RenderMesh debug_world{};
  const bool render_skeleton_only = snapshot.render_skeleton_only;
  const SkinnedModel *selected_player_model = snapshot.selected_player_model;
  const bool render_skinned_avatar =
      !render_skeleton_only && selected_player_model != nullptr;
  const PlayerEntity &local_player = *snapshot.local_player;

  append_vehicle_and_aircraft_debug(debug_world, snapshot);
  append_world_feature_debug(debug_world, snapshot, local_player);

  if (!snapshot.debug_collision_only) {
    append_player_debug(debug_world, local_player, *snapshot.local_player_animation,
                        selected_player_model, render_skinned_avatar,
                        render_skeleton_only, snapshot.collision_debug_enabled,
                        snapshot.devhud_enabled, glm::vec3(0.2f, 0.85f, 1.0f),
                        glm::vec3(0.95f, 0.97f, 1.0f), 0.012f, 0.12f);
  }

  if (snapshot.splitscreen && snapshot.local_player_secondary != nullptr &&
      snapshot.local_player_secondary_animation != nullptr &&
      !snapshot.debug_collision_only) {
    append_player_debug(debug_world, *snapshot.local_player_secondary,
                        *snapshot.local_player_secondary_animation,
                        selected_player_model, render_skinned_avatar,
                        render_skeleton_only, snapshot.collision_debug_enabled,
                        snapshot.devhud_enabled, glm::vec3(0.6f, 0.85f, 1.0f),
                        glm::vec3(0.86f, 0.92f, 1.0f), 0.010f, 0.10f);
  }

  append_collision_debug(debug_world, snapshot, local_player);
  append_remote_player_debug(debug_world, snapshot, local_player,
                             selected_player_model, render_skinned_avatar);
  return debug_world;
}
