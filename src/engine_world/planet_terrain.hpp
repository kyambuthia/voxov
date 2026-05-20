#pragma once

#include "engine_render/render_types.hpp"
#include "engine_world/planet_types.hpp"

class VoxelChunk;

RenderMesh build_single_face_planet_terrain_mesh(
    const PlanetDefinition &planet, const PlanetChunkId &chunk_id);

double planet_terrain_max_height_above_base(const PlanetDefinition &planet);
double planet_terrain_height_above_base_at_direction(
    const PlanetDefinition &planet, const glm::dvec3 &direction);

void stitch_face_edges(VoxelChunk &chunk, PlanetFace face, int32_t chunk_x,
                       int32_t chunk_y, const PlanetDefinition &planet);
