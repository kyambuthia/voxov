#include "engine_world/planet_math.hpp"

#include <algorithm>
#include <cmath>

#include <glm/geometric.hpp>

namespace {
constexpr double k_epsilon = 1.0e-12;

glm::dvec3 normalized_or(const glm::dvec3 &value,
                         const glm::dvec3 &fallback) {
  const double len2 = glm::dot(value, value);
  if (len2 <= k_epsilon) {
    return fallback;
  }
  return value / std::sqrt(len2);
}
} // namespace

glm::dvec3 face_uv_to_direction(PlanetFace face, double u, double v) {
  glm::dvec3 direction(0.0);
  switch (face) {
  case PlanetFace::PosX:
    direction = glm::dvec3(1.0, v, u);
    break;
  case PlanetFace::NegX:
    direction = glm::dvec3(-1.0, v, -u);
    break;
  case PlanetFace::PosY:
    direction = glm::dvec3(u, 1.0, v);
    break;
  case PlanetFace::NegY:
    direction = glm::dvec3(u, -1.0, -v);
    break;
  case PlanetFace::PosZ:
    direction = glm::dvec3(u, v, 1.0);
    break;
  case PlanetFace::NegZ:
    direction = glm::dvec3(-u, v, -1.0);
    break;
  }
  return normalized_or(direction, glm::dvec3(0.0, 1.0, 0.0));
}

PlanetFace direction_to_face(const glm::dvec3 &direction) {
  const glm::dvec3 d = normalized_or(direction, glm::dvec3(0.0, 1.0, 0.0));
  const glm::dvec3 abs_d = glm::abs(d);

  if (abs_d.x >= abs_d.y && abs_d.x >= abs_d.z) {
    return d.x >= 0.0 ? PlanetFace::PosX : PlanetFace::NegX;
  }
  if (abs_d.y >= abs_d.z) {
    return d.y >= 0.0 ? PlanetFace::PosY : PlanetFace::NegY;
  }
  return d.z >= 0.0 ? PlanetFace::PosZ : PlanetFace::NegZ;
}

PlanetFaceUV direction_to_face_uv(const glm::dvec3 &direction) {
  const glm::dvec3 d = normalized_or(direction, glm::dvec3(0.0, 1.0, 0.0));
  const PlanetFace face = direction_to_face(d);
  PlanetFaceUV uv{};
  uv.face = face;

  switch (face) {
  case PlanetFace::PosX:
    uv.u = d.z / d.x;
    uv.v = d.y / d.x;
    break;
  case PlanetFace::NegX:
    uv.u = d.z / d.x;
    uv.v = -d.y / d.x;
    break;
  case PlanetFace::PosY:
    uv.u = d.x / d.y;
    uv.v = d.z / d.y;
    break;
  case PlanetFace::NegY:
    uv.u = -d.x / d.y;
    uv.v = d.z / d.y;
    break;
  case PlanetFace::PosZ:
    uv.u = d.x / d.z;
    uv.v = d.y / d.z;
    break;
  case PlanetFace::NegZ:
    uv.u = d.x / d.z;
    uv.v = -d.y / d.z;
    break;
  }

  uv.u = std::clamp(uv.u, -1.0, 1.0);
  uv.v = std::clamp(uv.v, -1.0, 1.0);
  return uv;
}

PlanetChunkId neighbor_chunk_id(const PlanetChunkId &id, int32_t dx,
                                int32_t dy, int32_t chunks_per_face) {
  PlanetChunkId result = id;
  const int32_t max_coord = std::max(0, chunks_per_face - 1);
  result.x = std::clamp(id.x + dx, 0, max_coord);
  result.y = std::clamp(id.y + dy, 0, max_coord);
  return result;
}

glm::dvec3 radial_up(const PlanetDefinition &planet,
                     const glm::dvec3 &world_pos) {
  return normalized_or(world_pos - planet.center, glm::dvec3(0.0, 1.0, 0.0));
}

PlanetTangentBasis tangent_basis(const glm::dvec3 &radial_up_value,
                                 const glm::dvec3 &world_up_or_pole_vector) {
  PlanetTangentBasis basis{};
  basis.up = normalized_or(radial_up_value, glm::dvec3(0.0, 1.0, 0.0));

  glm::dvec3 pole = normalized_or(world_up_or_pole_vector,
                                  glm::dvec3(0.0, 1.0, 0.0));
  if (std::abs(glm::dot(basis.up, pole)) > 0.98) {
    pole = glm::dvec3(0.0, 0.0, 1.0);
  }

  basis.east = normalized_or(glm::cross(basis.up, pole),
                             glm::dvec3(1.0, 0.0, 0.0));
  basis.north = normalized_or(glm::cross(basis.east, basis.up),
                              glm::dvec3(0.0, 0.0, 1.0));
  return basis;
}

glm::dvec3 voxel_world_pos(const PlanetDefinition &planet, PlanetFace face,
                           double u, double v, double height_above_base) {
  return planet.center +
         face_uv_to_direction(face, u, v) * (planet.radius + height_above_base);
}

glm::dvec3 local_face_voxel_to_cube_point(
    const PlanetDefinition &planet, const LocalFaceVoxelCoords &coords) {
  const double x = coords.xyz.x;
  const double z = coords.xyz.z;
  const double r = std::max(planet.radius, k_epsilon);

  switch (coords.face) {
  case PlanetFace::PosX:
    return glm::dvec3(r, z, x);
  case PlanetFace::NegX:
    return glm::dvec3(-r, z, -x);
  case PlanetFace::PosY:
    return glm::dvec3(x, r, z);
  case PlanetFace::NegY:
    return glm::dvec3(x, -r, -z);
  case PlanetFace::PosZ:
    return glm::dvec3(x, z, r);
  case PlanetFace::NegZ:
    return glm::dvec3(-x, z, -r);
  }

  return glm::dvec3(x, r, z);
}

glm::dvec3 local_face_voxel_to_world_sphere(
    const PlanetDefinition &planet, const LocalFaceVoxelCoords &coords) {
  const glm::dvec3 cube_point =
      local_face_voxel_to_cube_point(planet, coords);
  const glm::dvec3 direction =
      normalized_or(cube_point, glm::dvec3(0.0, 1.0, 0.0));
  return planet.center + direction * (planet.radius + coords.xyz.y);
}

LocalFaceVoxelCoords world_sphere_to_local_face_voxel(
    const PlanetDefinition &planet, const glm::dvec3 &world_pos) {
  const glm::dvec3 offset = world_pos - planet.center;
  const double radial_distance = std::sqrt(glm::dot(offset, offset));
  const glm::dvec3 direction =
      normalized_or(offset, glm::dvec3(0.0, 1.0, 0.0));
  const PlanetFaceUV face_uv = direction_to_face_uv(direction);

  LocalFaceVoxelCoords coords{};
  coords.face = face_uv.face;
  coords.xyz.x = face_uv.u * planet.radius;
  coords.xyz.y = radial_distance - planet.radius;
  coords.xyz.z = face_uv.v * planet.radius;
  return coords;
}

double cubed_sphere_distortion_factor(const LocalFaceVoxelCoords &coords,
                                      double face_half_extent) {
  constexpr double k_corner_distortion_factor = 1.6180339887498948482;
  const double extent = std::max(std::abs(face_half_extent), k_epsilon);
  const double u = std::clamp(coords.xyz.x / extent, -1.0, 1.0);
  const double v = std::clamp(coords.xyz.z / extent, -1.0, 1.0);
  const double corner_t = std::clamp((u * u + v * v) * 0.5, 0.0, 1.0);
  return 1.0 + (k_corner_distortion_factor - 1.0) * corner_t;
}
