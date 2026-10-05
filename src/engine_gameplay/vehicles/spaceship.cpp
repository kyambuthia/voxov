#include "engine_gameplay/vehicles/spaceship.hpp"

#include <algorithm>
#include <cmath>

namespace {

glm::dvec3 radial_up(const glm::dvec3 &position, const glm::dvec3 &center) {
  const glm::dvec3 radial = position - center;
  const double length = glm::length(radial);
  return length > 1.0e-9 ? radial / length : glm::dvec3(0.0, 1.0, 0.0);
}

// Upright orientation (local +Y = up) with the nose along the tangential
// part of `heading`.
glm::dquat upright(const glm::dvec3 &up, glm::dvec3 heading) {
  heading -= up * glm::dot(heading, up);
  if (glm::dot(heading, heading) < 1.0e-12) {
    const glm::dvec3 reference = std::fabs(up.y) < 0.99
                                     ? glm::dvec3(0.0, 1.0, 0.0)
                                     : glm::dvec3(0.0, 0.0, 1.0);
    heading = reference - up * glm::dot(reference, up);
  }
  const glm::dvec3 forward = glm::normalize(heading);
  const glm::dvec3 side = glm::normalize(glm::cross(up, forward));
  return glm::normalize(
      glm::quat_cast(glm::dmat3(side, up, glm::cross(side, up))));
}

double ground_radius(const SpaceshipEnvironment &environment,
                     const glm::dvec3 &direction) {
  return environment.ground_radius ? environment.ground_radius(direction)
                                   : environment.body_radius;
}

void append_box(RenderMesh &mesh, const glm::dvec3 &center,
                const glm::dvec3 &half, const glm::dmat3 &frame,
                const glm::dvec3 &origin, const glm::vec3 &color) {
  const glm::dvec3 corners[8] = {
      {-half.x, -half.y, -half.z}, {half.x, -half.y, -half.z},
      {-half.x, half.y, -half.z},  {half.x, half.y, -half.z},
      {-half.x, -half.y, half.z},  {half.x, -half.y, half.z},
      {-half.x, half.y, half.z},   {half.x, half.y, half.z}};
  // Counter-clockwise seen from outside, matching the opaque pipeline.
  constexpr uint32_t faces[6][4] = {{0, 2, 3, 1}, {4, 5, 7, 6},
                                    {0, 4, 6, 2}, {1, 3, 7, 5},
                                    {2, 6, 7, 3}, {0, 1, 5, 4}};
  for (const auto &face : faces) {
    const glm::dvec3 normal = glm::normalize(
        frame * glm::cross(corners[face[1]] - corners[face[0]],
                           corners[face[2]] - corners[face[0]]));
    const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
    for (uint32_t index : face) {
      const glm::dvec3 world = center + frame * corners[index];
      mesh.vertices.push_back(
          {glm::vec3(world - origin), color, glm::vec3(normal)});
    }
    mesh.indices.insert(mesh.indices.end(),
                        {base, base + 1, base + 2, base, base + 2, base + 3});
  }
}

} // namespace

glm::dvec3 spaceship_forward(const SpaceshipState &ship) {
  return ship.orientation * glm::dvec3(0.0, 0.0, 1.0);
}

glm::dvec3 spaceship_up(const SpaceshipState &ship) {
  return ship.orientation * glm::dvec3(0.0, 1.0, 0.0);
}

double spaceship_altitude(const SpaceshipState &ship,
                          const SpaceshipEnvironment &environment) {
  const glm::dvec3 up = radial_up(ship.position, environment.body_center);
  return glm::length(ship.position - environment.body_center) -
         ground_radius(environment, up);
}

SpaceshipState make_landed_spaceship(const glm::dvec3 &ground_point,
                                     const glm::dvec3 &body_center,
                                     const glm::dvec3 &heading,
                                     const SpaceshipTuning &tuning) {
  SpaceshipState ship{};
  const glm::dvec3 up = radial_up(ground_point, body_center);
  ship.position = ground_point + up * tuning.hull_clearance;
  ship.orientation = upright(up, heading);
  ship.landed = true;
  return ship;
}

void steer_spaceship(SpaceshipState &ship, const SpaceshipInput &input,
                     const SpaceshipTuning &tuning, double dt) {
  // Local axes: pitch up turns about -X, yaw right about -Y, and roll
  // (right wing down) about +Z, since +X points to the pilot's left.
  if (ship.landed) {
    const glm::dvec3 up = spaceship_up(ship);
    ship.orientation = glm::normalize(
        glm::angleAxis(-static_cast<double>(input.yaw_delta), up) *
        ship.orientation);
    return;
  }
  const double roll = static_cast<double>(input.roll_axis) * tuning.roll_rate *
                      std::max(dt, 0.0);
  ship.orientation = glm::normalize(
      ship.orientation *
      glm::angleAxis(static_cast<double>(input.pitch_delta),
                     glm::dvec3(-1.0, 0.0, 0.0)) *
      glm::angleAxis(static_cast<double>(input.yaw_delta),
                     glm::dvec3(0.0, -1.0, 0.0)) *
      glm::angleAxis(roll, glm::dvec3(0.0, 0.0, 1.0)));
}

void step_spaceship(SpaceshipState &ship, const SpaceshipInput &input,
                    const SpaceshipEnvironment &environment,
                    const SpaceshipTuning &tuning, double dt) {
  dt = std::max(dt, 0.0);
  const glm::dvec3 forward = spaceship_forward(ship);
  const glm::dvec3 up = spaceship_up(ship);

  const float engine_demand = std::clamp(
      std::fabs(input.thrust_axis) + (input.lift ? 0.6f : 0.0f), 0.0f, 1.0f);
  const float glow_target =
      engine_demand * (input.boost ? 1.0f : 0.65f);
  ship.thrust_level += (glow_target - ship.thrust_level) *
                       static_cast<float>(1.0 - std::exp(-8.0 * dt));

  if (ship.landed) {
    if (!input.lift && input.thrust_axis <= 0.1f) {
      ship.velocity = glm::dvec3(0.0);
      return;
    }
    ship.landed = false;
  }

  const glm::dvec3 offset = ship.position - environment.body_center;
  const double distance = std::max(glm::length(offset), 1.0e-6);
  const glm::dvec3 radial = offset / distance;
  const double gravity = environment.surface_gravity *
                         (environment.body_radius / distance) *
                         (environment.body_radius / distance);

  const double boost = input.boost ? tuning.boost_multiplier : 1.0;
  const double thrust_scale = input.thrust_axis >= 0.0f ? 1.0 : 0.5;
  glm::dvec3 acceleration =
      forward * (static_cast<double>(input.thrust_axis) * thrust_scale *
                 tuning.thrust_accel * boost);
  if (input.lift) {
    acceleration += up * tuning.lift_accel;
  }
  if (input.descend) {
    acceleration -= up * tuning.lift_accel;
  }
  acceleration -= radial * gravity;

  ship.velocity += acceleration * dt;
  const double altitude = distance - environment.body_radius;
  const double in_atmosphere = std::clamp(
      1.0 - altitude / std::max(environment.atmosphere_height, 1.0), 0.0, 1.0);
  const double drag = tuning.space_drag +
                      (tuning.atmosphere_drag - tuning.space_drag) *
                          in_atmosphere;
  ship.velocity *= std::exp(-drag * dt);
  ship.position += ship.velocity * dt;

  // Inside the atmosphere with no roll input, ease the wings back to level
  // so the ship stays controllable with mouse steering alone.
  if (in_atmosphere > 0.0 && input.roll_axis == 0.0f) {
    const glm::dvec3 nose = spaceship_forward(ship);
    const glm::dvec3 roof = spaceship_up(ship);
    glm::dvec3 target = radial - nose * glm::dot(radial, nose);
    if (glm::dot(target, target) > 1.0e-6) {
      target = glm::normalize(target);
      const double error = std::atan2(
          glm::dot(nose, glm::cross(roof, target)), glm::dot(roof, target));
      const double fraction =
          std::min(1.0, tuning.level_rate * in_atmosphere * dt);
      ship.orientation = glm::normalize(
          glm::angleAxis(error * fraction, nose) * ship.orientation);
    }
  }

  // Ground contact against the terrain heightfield.
  const glm::dvec3 new_up = radial_up(ship.position, environment.body_center);
  const double floor =
      ground_radius(environment, new_up) + tuning.hull_clearance;
  if (glm::length(ship.position - environment.body_center) < floor) {
    ship.position = environment.body_center + new_up * floor;
    const double inward = glm::dot(ship.velocity, new_up);
    if (inward < 0.0) {
      ship.velocity -= new_up * inward;
    }
    ship.velocity *= 0.6; // skid friction
    if (glm::length(ship.velocity) < tuning.safe_landing_speed &&
        !input.lift) {
      ship.landed = true;
      ship.velocity = glm::dvec3(0.0);
      ship.orientation = upright(new_up, spaceship_forward(ship));
    }
  }
}

RenderMesh build_spaceship_mesh(const SpaceshipState &ship,
                                const glm::dvec3 &origin) {
  RenderMesh mesh{};
  const glm::dmat3 frame = glm::mat3_cast(ship.orientation);
  const auto part = [&](const glm::dvec3 &local_center,
                        const glm::dvec3 &half, const glm::vec3 &color) {
    append_box(mesh, ship.position + frame * local_center, half, frame,
               origin, color);
  };
  const glm::vec3 hull(0.80f, 0.82f, 0.86f);
  const glm::vec3 nose(0.88f, 0.89f, 0.92f);
  const glm::vec3 glass(0.10f, 0.24f, 0.34f);
  const glm::vec3 accent(0.88f, 0.46f, 0.14f);
  const glm::vec3 dark(0.22f, 0.24f, 0.28f);
  const float glow = std::clamp(ship.thrust_level, 0.0f, 1.0f);
  const glm::vec3 exhaust = glm::mix(glm::vec3(0.35f, 0.12f, 0.08f),
                                     glm::vec3(1.0f, 0.78f, 0.30f), glow);

  part({0.0, 0.0, 0.0}, {0.9, 0.55, 2.6}, hull);
  part({0.0, -0.05, 2.95}, {0.6, 0.4, 0.5}, nose);
  part({0.0, 0.62, 1.15}, {0.55, 0.28, 0.8}, glass);
  part({0.0, 0.95, -2.0}, {0.1, 0.5, 0.6}, accent);
  for (const double side : {-1.0, 1.0}) {
    part({side * 2.0, -0.15, -0.4}, {1.2, 0.12, 1.3}, accent);
    part({side * 3.1, 0.05, -0.9}, {0.12, 0.3, 0.6}, hull);
    part({side * 0.75, -0.1, -2.85}, {0.4, 0.4, 0.45}, dark);
    part({side * 0.75, -0.1, -3.32 - 0.25 * glow},
         {0.3, 0.3, 0.05 + 0.25 * glow}, exhaust);
    part({side * 0.8, -0.85, 0.0}, {0.1, 0.25, 1.6}, dark);
  }
  return mesh;
}
