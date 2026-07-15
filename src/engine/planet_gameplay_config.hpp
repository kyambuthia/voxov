#pragma once

#include <cstdint>

// Runtime-facing scale profile for the single playable planet. BlockWorld
// keeps generic defaults; Engine passes this compact profile to each system.
struct PlanetGameplayConfig {
  double radius_m = 64.0;
  double voxel_size_m = 1.0;

  int32_t surface_shells = 2;
  int32_t base_resolution = 32;
  int32_t chunk_size = 16;
  double terrain_feature_size_m = 24.0;
  float terrain_base_height_blocks = 4.0f;
  float terrain_amplitude_blocks = 3.0f;
  int32_t terrain_min_height_blocks = 1;
  int32_t terrain_max_height_blocks = 8;
  int32_t terrain_shell_margin_blocks = 1;

  double local_terrain_max_altitude_m = 32.0;
  int32_t global_surface_subdivisions = 64;
  double global_surface_radial_bias_m = 0.75;
  double atmosphere_height_m = 32.0;
  bool atmosphere_enabled = false;
  bool flight_vehicle_enabled = false;

  float debug_flight_speed_mps = 20.0f;
  float debug_flight_sprint_base_mps = 40.0f;
  float debug_flight_sprint_altitude_scale = 0.75f;
  float debug_flight_sprint_max_mps = 160.0f;
  float debug_flight_acceleration_mps2 = 120.0f;

  double capture_orbit_altitude_m = 128.0;
  double capture_flight_altitude_m = 48.0;
};

inline constexpr PlanetGameplayConfig kPlayablePlanetConfig{};
