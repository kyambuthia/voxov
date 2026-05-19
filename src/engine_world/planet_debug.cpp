#include "engine_world/planet_debug.hpp"

#include "engine_world/planet_math.hpp"

#include <algorithm>
#include <cmath>

#include <glm/geometric.hpp>

namespace {
constexpr PlanetFace k_faces[] = {
    PlanetFace::PosX, PlanetFace::NegX, PlanetFace::PosY,
    PlanetFace::NegY, PlanetFace::PosZ, PlanetFace::NegZ,
};

glm::vec3 face_color(PlanetFace face) {
  switch (face) {
  case PlanetFace::PosX:
    return glm::vec3(0.95f, 0.12f, 0.10f);
  case PlanetFace::NegX:
    return glm::vec3(0.40f, 0.04f, 0.035f);
  case PlanetFace::PosY:
    return glm::vec3(0.12f, 0.82f, 0.18f);
  case PlanetFace::NegY:
    return glm::vec3(0.035f, 0.34f, 0.08f);
  case PlanetFace::PosZ:
    return glm::vec3(0.12f, 0.34f, 1.0f);
  case PlanetFace::NegZ:
    return glm::vec3(0.035f, 0.08f, 0.42f);
  }
  return glm::vec3(1.0f);
}

glm::vec3 sphere_point(const PlanetDefinition &planet, PlanetFace face,
                       double u, double v, double radius_offset = 0.0) {
  const glm::dvec3 direction = face_uv_to_direction(face, u, v);
  return glm::vec3(planet.center + direction * (planet.radius + radius_offset));
}

RenderVertex debug_vertex(const PlanetDefinition &planet, PlanetFace face,
                          double u, double v, const glm::vec3 &color,
                          double radius_offset = 0.0) {
  const glm::dvec3 direction = face_uv_to_direction(face, u, v);
  return RenderVertex{
      glm::vec3(planet.center + direction * (planet.radius + radius_offset)),
      color,
      glm::vec3(direction),
  };
}

void append_oriented_quad(RenderMesh &mesh, const RenderVertex &a,
                          const RenderVertex &b, const RenderVertex &c,
                          const RenderVertex &d) {
  const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
  mesh.vertices.push_back(a);
  mesh.vertices.push_back(b);
  mesh.vertices.push_back(c);
  mesh.vertices.push_back(d);

  const glm::vec3 radial =
      glm::normalize(a.normal + b.normal + c.normal + d.normal);
  const glm::vec3 tri_normal = glm::normalize(
      glm::cross(b.position - a.position, c.position - a.position));
  if (glm::dot(tri_normal, radial) >= 0.0f) {
    mesh.indices.insert(mesh.indices.end(),
                        {base, base + 1, base + 2, base, base + 2, base + 3});
  } else {
    mesh.indices.insert(mesh.indices.end(),
                        {base, base + 2, base + 1, base, base + 3, base + 2});
  }
}

void append_line_box(RenderMesh &mesh, const glm::vec3 &center,
                     const glm::vec3 &start, const glm::vec3 &end,
                     float half_width, const glm::vec3 &color) {
  const glm::vec3 delta = end - start;
  const float len = glm::length(delta);
  if (len <= 1.0e-5f) {
    return;
  }

  const glm::vec3 dir = delta / len;
  glm::vec3 radial = (start + end) * 0.5f - center;
  if (glm::length(radial) <= 1.0e-5f) {
    radial = glm::vec3(0.0f, 1.0f, 0.0f);
  }
  glm::vec3 side = glm::cross(dir, glm::normalize(radial));
  if (glm::length(side) <= 1.0e-5f) {
    side = glm::cross(dir, glm::vec3(0.0f, 1.0f, 0.0f));
  }
  if (glm::length(side) <= 1.0e-5f) {
    side = glm::cross(dir, glm::vec3(1.0f, 0.0f, 0.0f));
  }
  side = glm::normalize(side) * half_width;
  const glm::vec3 up = glm::normalize(glm::cross(side, dir)) * half_width;

  const glm::vec3 a = start - side - up;
  const glm::vec3 b = start + side - up;
  const glm::vec3 c = start + side + up;
  const glm::vec3 d = start - side + up;
  const glm::vec3 e = end - side - up;
  const glm::vec3 f = end + side - up;
  const glm::vec3 g = end + side + up;
  const glm::vec3 h = end - side + up;

  auto quad = [&](const glm::vec3 &p0, const glm::vec3 &p1, const glm::vec3 &p2,
                  const glm::vec3 &p3) {
    const glm::vec3 normal = glm::normalize(glm::cross(p1 - p0, p2 - p0));
    append_oriented_quad(mesh, {p0, color, normal}, {p1, color, normal},
                         {p2, color, normal}, {p3, color, normal});
  };

  quad(a, b, f, e);
  quad(d, h, g, c);
  quad(a, e, h, d);
  quad(b, c, g, f);
  quad(a, d, c, b);
  quad(e, f, g, h);
}

void append_face_grid(RenderMesh &mesh, const PlanetDefinition &planet,
                      PlanetFace face, int32_t subdivisions,
                      float line_thickness, const glm::vec3 &color) {
  const int32_t clamped = std::max(1, subdivisions);
  const int32_t segments = std::max(4, clamped * 4);
  const double radius_offset = static_cast<double>(line_thickness) * 1.5;

  auto uv = [clamped](int32_t i) {
    return -1.0 + 2.0 * static_cast<double>(i) / static_cast<double>(clamped);
  };

  for (int32_t i = 0; i <= clamped; ++i) {
    const double fixed_u = uv(i);
    const double fixed_v = uv(i);
    for (int32_t s = 0; s < segments; ++s) {
      const double t0 =
          -1.0 + 2.0 * static_cast<double>(s) / static_cast<double>(segments);
      const double t1 = -1.0 + 2.0 * static_cast<double>(s + 1) /
                                   static_cast<double>(segments);
      append_line_box(mesh, glm::vec3(planet.center),
                      sphere_point(planet, face, fixed_u, t0, radius_offset),
                      sphere_point(planet, face, fixed_u, t1, radius_offset),
                      line_thickness, color);
      append_line_box(mesh, glm::vec3(planet.center),
                      sphere_point(planet, face, t0, fixed_v, radius_offset),
                      sphere_point(planet, face, t1, fixed_v, radius_offset),
                      line_thickness, color);
    }
  }
}
} // namespace

RenderMesh build_debug_planet_mesh(const PlanetDefinition &planet,
                                   int32_t grid_size) {
  RenderMesh mesh{};
  const int32_t clamped_grid = std::max(1, grid_size);
  mesh.vertices.reserve(
      static_cast<size_t>(6 * clamped_grid * clamped_grid * 4));
  mesh.indices.reserve(
      static_cast<size_t>(6 * clamped_grid * clamped_grid * 6));

  for (PlanetFace face : k_faces) {
    const glm::vec3 color = face_color(face);
    for (int32_t y = 0; y < clamped_grid; ++y) {
      const double v0 = -1.0 + 2.0 * static_cast<double>(y) /
                                   static_cast<double>(clamped_grid);
      const double v1 = -1.0 + 2.0 * static_cast<double>(y + 1) /
                                   static_cast<double>(clamped_grid);
      for (int32_t x = 0; x < clamped_grid; ++x) {
        const double u0 = -1.0 + 2.0 * static_cast<double>(x) /
                                     static_cast<double>(clamped_grid);
        const double u1 = -1.0 + 2.0 * static_cast<double>(x + 1) /
                                     static_cast<double>(clamped_grid);
        append_oriented_quad(mesh, debug_vertex(planet, face, u0, v0, color),
                             debug_vertex(planet, face, u1, v0, color),
                             debug_vertex(planet, face, u1, v1, color),
                             debug_vertex(planet, face, u0, v1, color));
      }
    }
  }
  return mesh;
}

RenderMesh build_debug_planet_grid_mesh(const PlanetDefinition &planet,
                                        int32_t subdivisions,
                                        float line_thickness) {
  RenderMesh mesh{};
  const glm::vec3 color(0.98f, 0.98f, 0.90f);
  for (PlanetFace face : k_faces) {
    append_face_grid(mesh, planet, face, subdivisions, line_thickness, color);
  }
  return mesh;
}

RenderMesh
build_debug_planet_face_highlight_mesh(const PlanetDefinition &planet,
                                       PlanetFace face, float line_thickness) {
  RenderMesh mesh{};
  append_face_grid(mesh, planet, face, 1, line_thickness,
                   glm::vec3(1.0f, 0.94f, 0.12f));
  return mesh;
}

PlanetFace debug_planet_camera_face(const PlanetDefinition &planet,
                                    const glm::dvec3 &camera_position,
                                    const glm::dvec3 &camera_forward) {
  if (glm::dot(camera_forward, camera_forward) <= 1.0e-12) {
    return PlanetFace::PosZ;
  }
  const glm::dvec3 dir = glm::normalize(camera_forward);
  const glm::dvec3 origin_to_center = camera_position - planet.center;
  const double b = 2.0 * glm::dot(origin_to_center, dir);
  const double c = glm::dot(origin_to_center, origin_to_center) -
                   planet.radius * planet.radius;
  const double discriminant = b * b - 4.0 * c;
  if (discriminant >= 0.0) {
    const double root = std::sqrt(discriminant);
    const double t0 = (-b - root) * 0.5;
    const double t1 = (-b + root) * 0.5;
    const double t = t0 >= 0.0 ? t0 : t1;
    if (t >= 0.0) {
      return direction_to_face(camera_position + dir * t - planet.center);
    }
  }
  return direction_to_face(dir);
}

const char *planet_face_debug_name(PlanetFace face) {
  switch (face) {
  case PlanetFace::PosX:
    return "+X";
  case PlanetFace::NegX:
    return "-X";
  case PlanetFace::PosY:
    return "+Y";
  case PlanetFace::NegY:
    return "-Y";
  case PlanetFace::PosZ:
    return "+Z";
  case PlanetFace::NegZ:
    return "-Z";
  }
  return "?";
}
