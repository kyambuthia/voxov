#pragma once

#include "engine_render/render_types.hpp"
#include "engine_world/planet_types.hpp"

#include <cstdint>

#include <glm/glm.hpp>

RenderMesh build_debug_planet_mesh(const PlanetDefinition &planet,
                                   int32_t grid_size);
RenderMesh build_planet_impostor_mesh(const PlanetDefinition &planet,
                                      int subdivisions);
RenderMesh build_debug_planet_grid_mesh(const PlanetDefinition &planet,
                                        int32_t subdivisions,
                                        float line_thickness);
RenderMesh
build_debug_planet_face_highlight_mesh(const PlanetDefinition &planet,
                                       PlanetFace face, float line_thickness);
PlanetFace debug_planet_camera_face(const PlanetDefinition &planet,
                                    const glm::dvec3 &camera_position,
                                    const glm::dvec3 &camera_forward);
const char *planet_face_debug_name(PlanetFace face);
