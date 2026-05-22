#pragma once

#include <cstdint>

#include <glm/glm.hpp>

enum class PlanetFace : uint8_t {
  PosX = 0,
  NegX = 1,
  PosY = 2,
  NegY = 3,
  PosZ = 4,
  NegZ = 5,
};

struct PlanetDefinition {
  glm::dvec3 center{0.0};
  double radius = 1024.0;
  double voxel_size = 1.0;
  int32_t chunks_per_face = 64;
  uint64_t seed = 0;
};

struct PlanetChunkId {
  PlanetFace face = PlanetFace::PosY;
  int32_t x = 0;
  int32_t y = 0;
  int32_t lod = 0;

  friend bool operator==(const PlanetChunkId &a,
                         const PlanetChunkId &b) = default;
};

struct PlanetFaceUV {
  PlanetFace face = PlanetFace::PosY;
  double u = 0.0;
  double v = 0.0;
};

struct LocalFaceVoxelCoords {
  PlanetFace face = PlanetFace::PosY;
  // x/z are local face-plane offsets in world units from the face center.
  // y is altitude above the planet base radius in world units.
  glm::dvec3 xyz{0.0};
};

struct PlanetTangentBasis {
  glm::dvec3 east{1.0, 0.0, 0.0};
  glm::dvec3 north{0.0, 0.0, 1.0};
  glm::dvec3 up{0.0, 1.0, 0.0};
};
