#pragma once

#include "engine_render/render_types.hpp"

#include <cstdint>

struct PlanetDefinition;

// ---------------------------------------------------------------------------
// Wireframe voxel planet mesh generation
//
// Generates a cube-sphere subdivided into a voxel grid, rendered as
// wireframe lines (SG_PRIMITIVETYPE_LINES).  Each face is colored
// distinctly so the cube-sphere structure is visible.
//
// `voxels_per_face_edge` controls the grid density.  A value of 8
// produces 8×8 voxels per face (512 voxels total) with wireframe
// edges drawn for every voxel cell.
// ---------------------------------------------------------------------------

RenderMesh build_wireframe_voxel_planet_mesh(const PlanetDefinition &planet,
                                              int32_t voxels_per_face_edge);

// A lightweight dashed shell used as a visual atmosphere treatment while the
// full scattering pass remains disabled or under development.
RenderMesh build_atmosphere_wireframe_mesh(const PlanetDefinition &planet,
                                            double atmosphere_height,
                                            int32_t latitude_lines = 12,
                                            int32_t longitude_lines = 24,
                                            int32_t dash_segments = 48);
