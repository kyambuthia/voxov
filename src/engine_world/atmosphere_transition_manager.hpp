#pragma once

#include "engine_world/planet_types.hpp"

#include <glm/glm.hpp>

enum class PlanetRenderState {
  Space = 0,
  Descending = 1,
  Surface = 2,
  Ascending = 3,
};

struct AtmosphereTransitionConfig {
  double surface_altitude = 96.0;
  double space_altitude = 192.0;
  double fade_seconds = 1.25;
};

struct AtmosphereTransitionSnapshot {
  PlanetRenderState state = PlanetRenderState::Space;
  PlanetFace active_face = PlanetFace::PosY;
  LocalFaceVoxelCoords player_local{};
  glm::dvec3 frozen_velocity{0.0};
  double space_alpha = 1.0;
  double surface_alpha = 0.0;
  double distortion_scale = 1.0;
  bool velocity_frozen = false;
  bool surface_physics_active = false;
};

class AtmosphereTransitionManager {
public:
  void configure(const AtmosphereTransitionConfig &config);
  void reset_to_space();
  void reset_to_surface(const PlanetDefinition &planet,
                        const glm::dvec3 &world_position);

  void update(const PlanetDefinition &planet, const glm::dvec3 &world_position,
              const glm::dvec3 &velocity, double dt);

  const AtmosphereTransitionSnapshot &snapshot() const { return snapshot_; }

private:
  void begin_descent(const PlanetDefinition &planet,
                     const glm::dvec3 &world_position,
                     const glm::dvec3 &velocity);
  void begin_ascent(const PlanetDefinition &planet,
                    const glm::dvec3 &world_position,
                    const glm::dvec3 &velocity);
  void finish_surface();
  void finish_space();
  void refresh_landing_metrics(const PlanetDefinition &planet,
                               const glm::dvec3 &world_position);
  void apply_transition_alpha(bool surface_in);

  AtmosphereTransitionConfig config_{};
  AtmosphereTransitionSnapshot snapshot_{};
  double transition_t_ = 0.0;
};
