#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

struct RenderVertex {
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::vec3 color{1.0f, 1.0f, 1.0f};
    glm::vec3 normal{0.0f, 0.0f, 0.0f};
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
    bool net_connected = false;
    uint32_t net_local_player_id = 0;
    uint32_t net_remote_count = 0;
    bool menu_open = false;
    int menu_selected = 0;
    std::string menu_title;
    std::vector<std::string> menu_items;
    std::vector<std::string> menu_guide;
    std::string menu_status;
    std::string menu_text;
};
