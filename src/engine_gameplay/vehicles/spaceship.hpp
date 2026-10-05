#pragma once

#include "engine_render/render_types.hpp"

#include <functional>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

// Arcade spaceship for hopping between nearby bodies. All positions are in
// the active body's frame (body centre at `SpaceshipEnvironment::body_center`).
// Orientation maps local +Z to the nose, +Y to the cabin roof, and
// +X = up x forward.
struct SpaceshipState {
  glm::dvec3 position{0.0}; // hull centre
  glm::dvec3 velocity{0.0};
  glm::dquat orientation{1.0, 0.0, 0.0, 0.0};
  bool landed = true;
  float thrust_level = 0.0f; // 0..1 smoothed, drives the exhaust glow
};

struct SpaceshipInput {
  float pitch_delta = 0.0f; // radians this frame (nose up positive)
  float yaw_delta = 0.0f;   // radians this frame (nose right positive)
  float roll_axis = 0.0f;   // -1..1, right wing down positive
  float thrust_axis = 0.0f; // -1..1 along the nose
  bool lift = false;        // thrust along the roof
  bool descend = false;     // thrust along the floor
  bool boost = false;
};

struct SpaceshipEnvironment {
  glm::dvec3 body_center{0.0};
  double body_radius = 64.0;
  double surface_gravity = 9.81; // m/s^2 at body_radius
  double atmosphere_height = 32.0;
  // Radial distance from the body centre to the ground along a unit
  // direction. Empty means the base sphere.
  std::function<double(const glm::dvec3 &)> ground_radius;
};

struct SpaceshipTuning {
  double thrust_accel = 30.0;   // m/s^2 along the nose at full throttle
  double boost_multiplier = 4.0;
  double lift_accel = 22.0;     // m/s^2 along the roof
  double roll_rate = 2.2;       // rad/s at full roll input
  double atmosphere_drag = 0.8; // 1/s velocity damping at the surface
  double space_drag = 0.05;     // 1/s velocity damping above the atmosphere
  double hull_clearance = 1.1;  // hull centre height above the ground
  double safe_landing_speed = 7.0;
  double level_rate = 3.0;      // 1/s auto-levelling toward radial up
  double max_speed = 250.0;     // m/s cap so the ship can always be stopped
};

// Applies steering for one rendered frame. Pitch and roll are locked while
// landed (only yaw turns the parked ship).
void steer_spaceship(SpaceshipState &ship, const SpaceshipInput &input,
                     const SpaceshipTuning &tuning, double dt);

// Integrates thrust, gravity, drag, and ground contact for one fixed step.
void step_spaceship(SpaceshipState &ship, const SpaceshipInput &input,
                    const SpaceshipEnvironment &environment,
                    const SpaceshipTuning &tuning, double dt);

glm::dvec3 spaceship_forward(const SpaceshipState &ship);
glm::dvec3 spaceship_up(const SpaceshipState &ship);
double spaceship_altitude(const SpaceshipState &ship,
                          const SpaceshipEnvironment &environment);

// Upright, landed ship standing on the ground at `ground_point`, nose along
// the tangential part of `heading`.
SpaceshipState make_landed_spaceship(const glm::dvec3 &ground_point,
                                     const glm::dvec3 &body_center,
                                     const glm::dvec3 &heading,
                                     const SpaceshipTuning &tuning);

// Blocky hull mesh. Vertices are relative to `origin` (pass the camera
// origin for camera-relative rendering).
RenderMesh build_spaceship_mesh(const SpaceshipState &ship,
                                const glm::dvec3 &origin);
