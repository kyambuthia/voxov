#pragma once

#include "engine_gameplay/player/player_components.hpp"

#include <cmath>

namespace player_surface_orientation {

inline glm::vec3 fallback_forward(const glm::vec3 &up) {
  glm::vec3 world_ref(0.0f, 1.0f, 0.0f);
  if (std::abs(glm::dot(up, world_ref)) > 0.99f) {
    world_ref = glm::vec3(0.0f, 0.0f, 1.0f);
  }
  const glm::vec3 east = glm::normalize(glm::cross(world_ref, up));
  return glm::normalize(glm::cross(up, east));
}

// Parallel-transport the reference heading by projecting it onto the new
// tangent plane. This keeps heading continuous across cube faces and poles.
inline glm::vec3 reference_forward(CameraRig &rig, const glm::vec3 &up) {
  glm::vec3 forward = rig.surface_reference_forward;
  if (rig.surface_frame_initialized) {
    forward -= up * glm::dot(forward, up);
  }
  if (!rig.surface_frame_initialized || glm::dot(forward, forward) < 1.0e-6f) {
    forward = fallback_forward(up);
    rig.surface_frame_initialized = true;
  } else {
    forward = glm::normalize(forward);
  }
  rig.surface_reference_forward = forward;
  return forward;
}

inline glm::vec3 east_from_forward(const glm::vec3 &forward,
                                   const glm::vec3 &up) {
  return glm::normalize(glm::cross(forward, up));
}

} // namespace player_surface_orientation
