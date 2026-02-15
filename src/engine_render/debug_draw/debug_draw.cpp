#include "engine_render/debug_draw/debug_draw.hpp"

#include <cmath>
#include <glm/glm.hpp>

namespace {
void append_tri(RenderMesh &mesh, const RenderVertex &a, const RenderVertex &b, const RenderVertex &c) {
    const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back(a);
    mesh.vertices.push_back(b);
    mesh.vertices.push_back(c);
    mesh.indices.push_back(base + 0);
    mesh.indices.push_back(base + 1);
    mesh.indices.push_back(base + 2);
}

RenderMesh build_uv_sphere(glm::vec3 center, float radius, glm::vec3 color, int slices, int stacks) {
    RenderMesh mesh;
    for (int stack = 0; stack < stacks; ++stack) {
        const float v0 = static_cast<float>(stack) / static_cast<float>(stacks);
        const float v1 = static_cast<float>(stack + 1) / static_cast<float>(stacks);
        const float phi0 = v0 * 3.1415926535f;
        const float phi1 = v1 * 3.1415926535f;

        for (int slice = 0; slice < slices; ++slice) {
            const float u0 = static_cast<float>(slice) / static_cast<float>(slices);
            const float u1 = static_cast<float>(slice + 1) / static_cast<float>(slices);
            const float th0 = u0 * 6.283185307f;
            const float th1 = u1 * 6.283185307f;

            const glm::vec3 p00 = center + radius * glm::vec3(std::sin(phi0) * std::cos(th0), std::cos(phi0), std::sin(phi0) * std::sin(th0));
            const glm::vec3 p01 = center + radius * glm::vec3(std::sin(phi0) * std::cos(th1), std::cos(phi0), std::sin(phi0) * std::sin(th1));
            const glm::vec3 p10 = center + radius * glm::vec3(std::sin(phi1) * std::cos(th0), std::cos(phi1), std::sin(phi1) * std::sin(th0));
            const glm::vec3 p11 = center + radius * glm::vec3(std::sin(phi1) * std::cos(th1), std::cos(phi1), std::sin(phi1) * std::sin(th1));

            append_tri(mesh, {p00, color}, {p10, color}, {p11, color});
            append_tri(mesh, {p00, color}, {p11, color}, {p01, color});
        }
    }
    return mesh;
}

RenderMesh build_cylinder(glm::vec3 base_center, float radius, float height, glm::vec3 color, int slices) {
    RenderMesh mesh;
    const glm::vec3 top_center = base_center + glm::vec3(0.0f, height, 0.0f);

    for (int slice = 0; slice < slices; ++slice) {
        const float t0 = (static_cast<float>(slice) / static_cast<float>(slices)) * 6.283185307f;
        const float t1 = (static_cast<float>(slice + 1) / static_cast<float>(slices)) * 6.283185307f;

        const glm::vec3 b0 = base_center + glm::vec3(std::cos(t0) * radius, 0.0f, std::sin(t0) * radius);
        const glm::vec3 b1 = base_center + glm::vec3(std::cos(t1) * radius, 0.0f, std::sin(t1) * radius);
        const glm::vec3 t0p = top_center + glm::vec3(std::cos(t0) * radius, 0.0f, std::sin(t0) * radius);
        const glm::vec3 t1p = top_center + glm::vec3(std::cos(t1) * radius, 0.0f, std::sin(t1) * radius);

        append_tri(mesh, {b0, color}, {t0p, color}, {t1p, color});
        append_tri(mesh, {b0, color}, {t1p, color}, {b1, color});
    }

    return mesh;
}
}

void append_mesh(RenderMesh &dst, const RenderMesh &src) {
    const uint32_t base = static_cast<uint32_t>(dst.vertices.size());
    dst.vertices.insert(dst.vertices.end(), src.vertices.begin(), src.vertices.end());
    for (uint32_t idx : src.indices) {
        dst.indices.push_back(base + idx);
    }
}

RenderMesh build_debug_sphere_mesh(glm::vec3 center, float radius, glm::vec3 color) {
    return build_uv_sphere(center, radius, color, 12, 8);
}

RenderMesh build_debug_capsule_mesh(glm::vec3 feet_position, float radius, float height, glm::vec3 color) {
    RenderMesh mesh;

    const float cylinder_height = std::max(0.0f, height - (2.0f * radius));
    const glm::vec3 bottom_center = feet_position + glm::vec3(0.0f, radius, 0.0f);
    const glm::vec3 top_center = bottom_center + glm::vec3(0.0f, cylinder_height, 0.0f);

    append_mesh(mesh, build_cylinder(bottom_center, radius, cylinder_height, color, 16));

    RenderMesh bottom = build_uv_sphere(bottom_center, radius, color, 12, 8);
    RenderMesh top = build_uv_sphere(top_center, radius, color, 12, 8);
    append_mesh(mesh, bottom);
    append_mesh(mesh, top);

    return mesh;
}
