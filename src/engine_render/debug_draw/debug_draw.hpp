#pragma once

#include "engine_render/render_types.hpp"

#include <glm/glm.hpp>

RenderMesh build_debug_capsule_mesh(glm::vec3 feet_position, float radius, float height, glm::vec3 color);
RenderMesh build_debug_sphere_mesh(glm::vec3 center, float radius, glm::vec3 color);
void append_mesh(RenderMesh &dst, const RenderMesh &src);
