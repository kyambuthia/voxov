#pragma once

#include <string>

#include "engine_math/camera.hpp"
#include "engine_render/render_types.hpp"

RenderMesh build_camera_text_mesh(const Camera &camera, const std::string &text);
