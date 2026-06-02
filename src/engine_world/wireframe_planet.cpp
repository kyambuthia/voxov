#include "engine_world/wireframe_planet.hpp"
#include "engine_world/planet.hpp"

#include <glm/glm.hpp>
#include <vector>

namespace {

// Cube-sphere face UV → world position on the sphere surface.
glm::dvec3 face_uv_to_sphere_pos(const PlanetDefinition &planet,
                                  PlanetFace face,
                                  double u, double v) {
  const glm::dvec3 dir = face_uv_to_direction(face, u, v);
  return planet.center + dir * planet.radius;
}

// Face-color palette so each of the 6 faces is visually distinct.
glm::vec3 face_wireframe_color(PlanetFace face) {
  switch (face) {
    case PlanetFace::PosX:  return glm::vec3(1.00f, 0.30f, 0.30f); // red
    case PlanetFace::NegX:  return glm::vec3(0.30f, 0.30f, 1.00f); // blue
    case PlanetFace::PosY:  return glm::vec3(0.30f, 1.00f, 0.30f); // green
    case PlanetFace::NegY:  return glm::vec3(1.00f, 0.80f, 0.20f); // yellow
    case PlanetFace::PosZ:  return glm::vec3(1.00f, 0.40f, 0.80f); // magenta
    case PlanetFace::NegZ:  return glm::vec3(0.40f, 1.00f, 1.00f); // cyan
  }
  return glm::vec3(1.0f, 1.0f, 1.0f);
}

// Add a single line segment to the mesh.
void add_line(RenderMesh &mesh,
              const glm::dvec3 &a,
              const glm::dvec3 &b,
              const glm::vec3 &color) {
  mesh.vertices.push_back(RenderVertex{glm::vec3(a), color, glm::vec3(0.0f)});
  mesh.vertices.push_back(RenderVertex{glm::vec3(b), color, glm::vec3(0.0f)});
  mesh.indices.push_back(static_cast<uint32_t>(mesh.vertices.size() - 2));
  mesh.indices.push_back(static_cast<uint32_t>(mesh.vertices.size() - 1));
}

} // namespace

RenderMesh build_wireframe_voxel_planet_mesh(const PlanetDefinition &planet,
                                              int32_t voxels_per_face_edge) {
  RenderMesh mesh{};
  mesh.use_16_bit_indices = false;

  const int32_t n = std::max(1, voxels_per_face_edge);

  // Estimate: each face has (n+1) horizontal lines × (n+1) segments
  //           + (n+1) vertical lines × (n+1) segments.
  // Each segment = 1 line = 2 vertices, 2 indices.
  const size_t lines_per_face = 2 * (n + 1) * (n + 1);
  mesh.vertices.reserve(2 * lines_per_face * 6);
  mesh.indices.reserve(2 * lines_per_face * 6);

  const PlanetFace faces[] = {
      PlanetFace::PosX, PlanetFace::NegX,
      PlanetFace::PosY, PlanetFace::NegY,
      PlanetFace::PosZ, PlanetFace::NegZ,
  };

  for (PlanetFace face : faces) {
    const glm::vec3 color = face_wireframe_color(face);

    for (int32_t i = 0; i <= n; ++i) {
      const double t = static_cast<double>(i) / static_cast<double>(n);
      const double fixed = -1.0 + 2.0 * t;

      // Horizontal line at v = fixed, sweeping u.
      for (int32_t j = 0; j < n; ++j) {
        const double u0 = -1.0 + 2.0 * static_cast<double>(j) / static_cast<double>(n);
        const double u1 = -1.0 + 2.0 * static_cast<double>(j + 1) / static_cast<double>(n);
        add_line(mesh,
                 face_uv_to_sphere_pos(planet, face, u0, fixed),
                 face_uv_to_sphere_pos(planet, face, u1, fixed),
                 color);
      }

      // Vertical line at u = fixed, sweeping v.
      for (int32_t j = 0; j < n; ++j) {
        const double v0 = -1.0 + 2.0 * static_cast<double>(j) / static_cast<double>(n);
        const double v1 = -1.0 + 2.0 * static_cast<double>(j + 1) / static_cast<double>(n);
        add_line(mesh,
                 face_uv_to_sphere_pos(planet, face, fixed, v0),
                 face_uv_to_sphere_pos(planet, face, fixed, v1),
                 color);
      }
    }
  }

  // Stable ID so the renderer can cache it.
  mesh.mesh_id = 0x574952454652414dull; // "WIREFRAM"
  return mesh;
}
