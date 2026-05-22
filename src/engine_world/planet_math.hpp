#pragma once

#include <cstdint>

#include <glm/glm.hpp>

#include "engine_world/planet_types.hpp"

glm::dvec3 face_uv_to_direction(PlanetFace face, double u, double v);
PlanetFace direction_to_face(const glm::dvec3 &direction);
PlanetFaceUV direction_to_face_uv(const glm::dvec3 &direction);
PlanetChunkId neighbor_chunk_id(const PlanetChunkId &id, int32_t dx,
                                int32_t dy, int32_t chunks_per_face);
glm::dvec3 radial_up(const PlanetDefinition &planet,
                     const glm::dvec3 &world_pos);
PlanetTangentBasis tangent_basis(
    const glm::dvec3 &radial_up,
    const glm::dvec3 &world_up_or_pole_vector = glm::dvec3(0.0, 1.0, 0.0));
glm::dvec3 voxel_world_pos(const PlanetDefinition &planet, PlanetFace face,
                           double u, double v, double height_above_base);
glm::dvec3 local_face_voxel_to_cube_point(const PlanetDefinition &planet,
                                          const LocalFaceVoxelCoords &coords);
glm::dvec3 local_face_voxel_to_world_sphere(
    const PlanetDefinition &planet, const LocalFaceVoxelCoords &coords);
LocalFaceVoxelCoords world_sphere_to_local_face_voxel(
    const PlanetDefinition &planet, const glm::dvec3 &world_pos);
double cubed_sphere_distortion_factor(const LocalFaceVoxelCoords &coords,
                                      double face_half_extent);
