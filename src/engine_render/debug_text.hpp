#pragma once

#include <string>

#include "engine_math/camera.hpp"
#include "engine_render/render_types.hpp"

RenderMesh build_camera_text_mesh(const Camera &camera, const std::string &text);
RenderMesh build_screen_text_mesh(
    const std::string &text,
    float origin_x_ndc,
    float origin_y_ndc,
    float cell_size_ndc,
    const glm::vec3 &color,
    float line_spacing_scale = 1.0f);
