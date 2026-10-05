#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

// Client-side presentation state for a replicated player. Server snapshots
// arrive at the network tick rate; this smooths them for rendering and
// derives a facing direction, which the snapshot doesn't carry.
struct RemoteAvatarState {
  glm::dvec3 feet{0.0};
  glm::vec3 forward{0.0f, 0.0f, 1.0f};
  bool initialized = false;
};

// Moves the avatar toward the latest replicated feet position (snapping on
// large corrections such as respawns) and turns it toward its horizontal
// velocity while moving. A still avatar keeps its last heading.
void update_remote_avatar(RemoteAvatarState &avatar,
                          const glm::dvec3 &target_feet,
                          const glm::vec3 &velocity,
                          const glm::dvec3 &planet_center, float dt);

// Upright orientation (local +Y = radial up, +Z = forward) for rendering.
glm::quat remote_avatar_orientation(const RemoteAvatarState &avatar,
                                    const glm::dvec3 &planet_center);
