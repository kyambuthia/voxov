#pragma once

#include "engine_render/render_types.hpp"
#include "engine_world/planet_types.hpp"

class VoxelChunk;

struct PlanetChunkUvRange {
  double u0 = -1.0;
  double v0 = -1.0;
  double u1 = 1.0;
  double v1 = 1.0;
};

enum class PlanetTerrainRenderMode {
  SpaceCubedSphere = 0,
  SurfaceFlatFace = 1,
};

struct PlanetSurfaceRenderFrame {
  PlanetFace face = PlanetFace::PosY;
  glm::dvec3 camera_local_origin{0.0};
  double distortion_scale = 1.0;
};

PlanetChunkUvRange planet_chunk_uv_range(const PlanetChunkId &chunk_id);

RenderMesh build_single_face_planet_terrain_mesh(
    const PlanetDefinition &planet, const PlanetChunkId &chunk_id);
RenderMesh build_planet_terrain_mesh(
    const PlanetDefinition &planet, const PlanetChunkId &chunk_id,
    PlanetTerrainRenderMode mode,
    const PlanetSurfaceRenderFrame &surface_frame = {});

double planet_terrain_max_height_above_base(const PlanetDefinition &planet);
double planet_terrain_height_above_base_at_direction(
    const PlanetDefinition &planet, const glm::dvec3 &direction);

void stitch_face_edges(VoxelChunk &chunk, PlanetFace face, int32_t chunk_x,
                       int32_t chunk_y, const PlanetDefinition &planet);
