#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

struct RenderVertex {
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::vec3 color{1.0f, 1.0f, 1.0f};
};

struct RenderMesh {
    std::vector<RenderVertex> vertices;
    std::vector<uint32_t> indices;
};

struct RenderScene {
    std::vector<RenderMesh> opaque_meshes;
    RenderMesh debug_grid;
    RenderMesh debug_world;
    RenderMesh debug_screen;
};

struct RenderStats {
    double fps = 0.0;
    double cpu_ms = 0.0;
};
