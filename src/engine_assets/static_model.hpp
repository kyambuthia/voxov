#pragma once

#include "engine_render/render_types.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <string>
#include <vector>

class StaticModel {
public:
    bool load_from_glb(const std::string &path, std::string &out_error);
    bool loaded() const;
    RenderMesh build_render_mesh(
        const glm::vec3 &world_position,
        const glm::quat &world_rotation,
        const glm::vec3 &color) const;

private:
    bool ready = false;
    float model_scale = 1.0f;
    float model_ground_lift = 0.0f;
    std::vector<glm::vec3> positions;
    std::vector<uint32_t> indices;
};
