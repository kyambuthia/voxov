#pragma once

#include "engine_render/render_types.hpp"
#include "engine_world/planet_types.hpp"

RenderMesh build_single_face_planet_terrain_mesh(
    const PlanetDefinition &planet, const PlanetChunkId &chunk_id);
