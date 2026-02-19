#include "engine_world/voxel_chunk.hpp"

#include <algorithm>
#include <cmath>

namespace {
constexpr glm::vec3 FACE_NORMALS[6] = {
    {1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f},
    {0.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 0.0f},
    {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}
};

constexpr glm::ivec3 FACE_DIRS[6] = {
    {1, 0, 0}, {-1, 0, 0},
    {0, 1, 0}, {0, -1, 0},
    {0, 0, 1}, {0, 0, -1}
};

constexpr glm::vec3 FACE_QUADS[6][4] = {
    {{1,0,0}, {1,1,0}, {1,1,1}, {1,0,1}},
    {{0,0,1}, {0,1,1}, {0,1,0}, {0,0,0}},
    {{0,1,1}, {1,1,1}, {1,1,0}, {0,1,0}},
    {{0,0,0}, {1,0,0}, {1,0,1}, {0,0,1}},
    {{0,0,1}, {1,0,1}, {1,1,1}, {0,1,1}},
    {{0,1,0}, {1,1,0}, {1,0,0}, {0,0,0}}
};

void append_quad(RenderMesh &mesh, const glm::vec3 &base, const glm::vec3 quad[4], const glm::vec3 &color) {
    uint32_t start = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back({base + quad[0], color});
    mesh.vertices.push_back({base + quad[1], color});
    mesh.vertices.push_back({base + quad[2], color});
    mesh.vertices.push_back({base + quad[3], color});

    mesh.indices.push_back(start + 0);
    mesh.indices.push_back(start + 1);
    mesh.indices.push_back(start + 2);
    mesh.indices.push_back(start + 0);
    mesh.indices.push_back(start + 2);
    mesh.indices.push_back(start + 3);
}
}

size_t VoxelChunk::index(int x, int y, int z) const {
    return static_cast<size_t>((z * CHUNK_Y * CHUNK_X) + (y * CHUNK_X) + x);
}

void VoxelChunk::generate_heightmap_terrain() {
    voxels.fill(0);
    constexpr float flat_height = 6.0f;
    constexpr float inner = 3.8f;
    constexpr float outer = 6.2f;
    const float cx = static_cast<float>(CHUNK_X - 1) * 0.5f;
    const float cz = static_cast<float>(CHUNK_Z - 1) * 0.5f;

    for (int z = 0; z < CHUNK_Z; ++z) {
        for (int x = 0; x < CHUNK_X; ++x) {
            float sx = static_cast<float>(x) / static_cast<float>(CHUNK_X);
            float sz = static_cast<float>(z) / static_cast<float>(CHUNK_Z);
            float h = 5.0f + std::sin(sx * 6.28f) * 2.0f + std::cos(sz * 9.42f) * 1.5f;
            const float dx = static_cast<float>(x) - cx;
            const float dz = static_cast<float>(z) - cz;
            const float ring_d = std::max(std::fabs(dx), std::fabs(dz));
            if (ring_d <= outer) {
                const float t = std::clamp((ring_d - inner) / std::max(0.001f, outer - inner), 0.0f, 1.0f);
                h = flat_height + (h - flat_height) * t;
            }
            int max_y = static_cast<int>(h);
            if (max_y < 1) {
                max_y = 1;
            }
            if (max_y >= CHUNK_Y) {
                max_y = CHUNK_Y - 1;
            }
            for (int y = 0; y <= max_y; ++y) {
                voxels[index(x, y, z)] = 1;
            }
        }
    }
}

void VoxelChunk::generate_flat_ground(int ground_y) {
    voxels.fill(0);
    const int max_y = std::clamp(ground_y, 0, CHUNK_Y - 1);
    for (int z = 0; z < CHUNK_Z; ++z) {
        for (int x = 0; x < CHUNK_X; ++x) {
            for (int y = 0; y <= max_y; ++y) {
                voxels[index(x, y, z)] = 1;
            }
        }
    }
}

bool VoxelChunk::solid(int x, int y, int z) const {
    if (x < 0 || y < 0 || z < 0 || x >= CHUNK_X || y >= CHUNK_Y || z >= CHUNK_Z) {
        return false;
    }
    return voxels[index(x, y, z)] != 0;
}

void VoxelChunk::set_solid(int x, int y, int z, bool value) {
    if (x < 0 || y < 0 || z < 0 || x >= CHUNK_X || y >= CHUNK_Y || z >= CHUNK_Z) {
        return;
    }
    voxels[index(x, y, z)] = value ? 1 : 0;
}

RenderMesh VoxelChunk::build_naive_mesh() const {
    RenderMesh mesh;

    for (int z = 0; z < CHUNK_Z; ++z) {
        for (int y = 0; y < CHUNK_Y; ++y) {
            for (int x = 0; x < CHUNK_X; ++x) {
                if (!solid(x, y, z)) {
                    continue;
                }

                glm::vec3 base(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
                float height_t = static_cast<float>(y) / static_cast<float>(CHUNK_Y);
                glm::vec3 grass(0.22f + height_t * 0.2f, 0.45f + height_t * 0.35f, 0.16f);
                glm::vec3 dirt(0.38f, 0.27f, 0.18f);

                for (int face = 0; face < 6; ++face) {
                    const glm::ivec3 n = FACE_DIRS[face];
                    if (solid(x + n.x, y + n.y, z + n.z)) {
                        continue;
                    }
                    const glm::vec3 color = (face == 2) ? grass : dirt;
                    append_quad(mesh, base, FACE_QUADS[face], color);
                }
            }
        }
    }

    return mesh;
}

RenderMesh VoxelChunk::build_debug_grid(float span, float step) const {
    RenderMesh mesh;
    const float y = -0.01f;
    const float half = span * 0.5f;

    auto add_line_quad = [&](glm::vec3 a, glm::vec3 b, float thickness, glm::vec3 color) {
        glm::vec3 dir = glm::normalize(b - a);
        glm::vec3 side(-dir.z, 0.0f, dir.x);
        side *= thickness * 0.5f;
        uint32_t start = static_cast<uint32_t>(mesh.vertices.size());
        mesh.vertices.push_back({a - side, color});
        mesh.vertices.push_back({a + side, color});
        mesh.vertices.push_back({b + side, color});
        mesh.vertices.push_back({b - side, color});
        mesh.indices.insert(mesh.indices.end(), {start, start + 1, start + 2, start, start + 2, start + 3});
    };

    for (float p = -half; p <= half; p += step) {
        glm::vec3 color = (std::fabs(p) < 0.001f) ? glm::vec3(0.6f, 0.6f, 0.8f) : glm::vec3(0.18f, 0.18f, 0.22f);
        add_line_quad({-half, y, p}, {half, y, p}, 0.04f, color);
        add_line_quad({p, y, -half}, {p, y, half}, 0.04f, color);
    }

    return mesh;
}

RenderMesh VoxelChunk::build_sky_placeholder(float size) const {
    RenderMesh mesh;
    const float h = size * 0.5f;

    glm::vec3 top(0.18f, 0.32f, 0.6f);
    glm::vec3 horizon(0.46f, 0.62f, 0.86f);

    uint32_t start = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back({{-h, h, -h}, top});
    mesh.vertices.push_back({{ h, h, -h}, top});
    mesh.vertices.push_back({{ h, -h, -h}, horizon});
    mesh.vertices.push_back({{-h, -h, -h}, horizon});
    mesh.indices.insert(mesh.indices.end(), {start, start + 1, start + 2, start, start + 2, start + 3});

    return mesh;
}
