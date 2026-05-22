#include "engine_world/atmosphere_transition_manager.hpp"

#include "engine_world/planet_math.hpp"

#include <algorithm>
#include <cmath>

#include <glm/geometric.hpp>

namespace {
double smoothstep(double t) {
  t = std::clamp(t, 0.0, 1.0);
  return t * t * (3.0 - 2.0 * t);
}
} // namespace

void AtmosphereTransitionManager::configure(
    const AtmosphereTransitionConfig &config) {
  config_ = config;
  if (config_.space_altitude < config_.surface_altitude) {
    std::swap(config_.space_altitude, config_.surface_altitude);
  }
  config_.fade_seconds = std::max(config_.fade_seconds, 0.001);
}

void AtmosphereTransitionManager::reset_to_space() {
  snapshot_ = AtmosphereTransitionSnapshot{};
  snapshot_.state = PlanetRenderState::Space;
  snapshot_.space_alpha = 1.0;
  snapshot_.surface_alpha = 0.0;
  snapshot_.velocity_frozen = false;
  snapshot_.surface_physics_active = false;
  transition_t_ = 0.0;
}

void AtmosphereTransitionManager::reset_to_surface(
    const PlanetDefinition &planet, const glm::dvec3 &world_position) {
  snapshot_ = AtmosphereTransitionSnapshot{};
  snapshot_.state = PlanetRenderState::Surface;
  snapshot_.space_alpha = 0.0;
  snapshot_.surface_alpha = 1.0;
  snapshot_.velocity_frozen = false;
  snapshot_.surface_physics_active = true;
  refresh_landing_metrics(planet, world_position);
  transition_t_ = 1.0;
}

void AtmosphereTransitionManager::update(const PlanetDefinition &planet,
                                         const glm::dvec3 &world_position,
                                         const glm::dvec3 &velocity,
                                         double dt) {
  const double altitude =
      glm::length(world_position - planet.center) - planet.radius;

  switch (snapshot_.state) {
  case PlanetRenderState::Space:
    if (altitude <= config_.surface_altitude) {
      begin_descent(planet, world_position, velocity);
    }
    break;
  case PlanetRenderState::Surface:
    refresh_landing_metrics(planet, world_position);
    if (altitude >= config_.space_altitude) {
      begin_ascent(planet, world_position, velocity);
    }
    break;
  case PlanetRenderState::Descending:
    refresh_landing_metrics(planet, world_position);
    transition_t_ += dt / config_.fade_seconds;
    apply_transition_alpha(true);
    if (transition_t_ >= 1.0) {
      finish_surface();
    }
    break;
  case PlanetRenderState::Ascending:
    refresh_landing_metrics(planet, world_position);
    transition_t_ += dt / config_.fade_seconds;
    apply_transition_alpha(false);
    if (transition_t_ >= 1.0) {
      finish_space();
    }
    break;
  }
}

void AtmosphereTransitionManager::begin_descent(
    const PlanetDefinition &planet, const glm::dvec3 &world_position,
    const glm::dvec3 &velocity) {
  snapshot_.state = PlanetRenderState::Descending;
  snapshot_.frozen_velocity = velocity;
  snapshot_.velocity_frozen = true;
  snapshot_.surface_physics_active = false;
  transition_t_ = 0.0;
  refresh_landing_metrics(planet, world_position);
  apply_transition_alpha(true);
}

void AtmosphereTransitionManager::begin_ascent(
    const PlanetDefinition &planet, const glm::dvec3 &world_position,
    const glm::dvec3 &velocity) {
  snapshot_.state = PlanetRenderState::Ascending;
  snapshot_.frozen_velocity = velocity;
  snapshot_.velocity_frozen = true;
  snapshot_.surface_physics_active = false;
  transition_t_ = 0.0;
  refresh_landing_metrics(planet, world_position);
  apply_transition_alpha(false);
}

void AtmosphereTransitionManager::finish_surface() {
  snapshot_.state = PlanetRenderState::Surface;
  snapshot_.space_alpha = 0.0;
  snapshot_.surface_alpha = 1.0;
  snapshot_.velocity_frozen = false;
  snapshot_.surface_physics_active = true;
  transition_t_ = 1.0;
}

void AtmosphereTransitionManager::finish_space() {
  snapshot_.state = PlanetRenderState::Space;
  snapshot_.space_alpha = 1.0;
  snapshot_.surface_alpha = 0.0;
  snapshot_.velocity_frozen = false;
  snapshot_.surface_physics_active = false;
  transition_t_ = 1.0;
}

void AtmosphereTransitionManager::refresh_landing_metrics(
    const PlanetDefinition &planet, const glm::dvec3 &world_position) {
  snapshot_.player_local =
      world_sphere_to_local_face_voxel(planet, world_position);
  snapshot_.active_face = snapshot_.player_local.face;
  snapshot_.distortion_scale =
      cubed_sphere_distortion_factor(snapshot_.player_local, planet.radius);
}

void AtmosphereTransitionManager::apply_transition_alpha(bool surface_in) {
  const double a = smoothstep(transition_t_);
  if (surface_in) {
    snapshot_.space_alpha = 1.0 - a;
    snapshot_.surface_alpha = a;
  } else {
    snapshot_.space_alpha = a;
    snapshot_.surface_alpha = 1.0 - a;
  }
}
