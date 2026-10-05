#include "engine_gameplay/player/remote_avatar.hpp"

#include <algorithm>
#include <cmath>

namespace {

glm::vec3 radial_up(const glm::dvec3 &position, const glm::dvec3 &center) {
  const glm::dvec3 radial = position - center;
  return glm::dot(radial, radial) > 1.0e-12
             ? glm::vec3(glm::normalize(radial))
             : glm::vec3(0.0f, 1.0f, 0.0f);
}

glm::vec3 tangent(const glm::vec3 &v, const glm::vec3 &up) {
  return v - up * glm::dot(v, up);
}

glm::vec3 any_tangent(const glm::vec3 &up) {
  const glm::vec3 reference = std::fabs(up.y) < 0.99f
                                  ? glm::vec3(0.0f, 1.0f, 0.0f)
                                  : glm::vec3(0.0f, 0.0f, 1.0f);
  return glm::normalize(tangent(reference, up));
}

} // namespace

void update_remote_avatar(RemoteAvatarState &avatar,
                          const glm::dvec3 &target_feet,
                          const glm::vec3 &velocity,
                          const glm::dvec3 &planet_center, float dt) {
  constexpr double kSnapDistance = 6.0;
  constexpr float kFollowRate = 14.0f;
  constexpr float kTurnRate = 10.0f;
  constexpr float kMinTurnSpeed = 0.35f;

  if (!avatar.initialized ||
      glm::length(target_feet - avatar.feet) > kSnapDistance) {
    avatar.feet = target_feet;
    avatar.initialized = true;
  } else {
    const double alpha = 1.0 - std::exp(-kFollowRate * std::max(dt, 0.0f));
    avatar.feet += (target_feet - avatar.feet) * alpha;
  }

  const glm::vec3 up = radial_up(avatar.feet, planet_center);
  glm::vec3 forward = tangent(avatar.forward, up);
  if (glm::dot(forward, forward) < 1.0e-8f) {
    forward = any_tangent(up);
  }
  forward = glm::normalize(forward);

  const glm::vec3 moving = tangent(velocity, up);
  if (glm::length(moving) > kMinTurnSpeed) {
    const float alpha = 1.0f - std::exp(-kTurnRate * std::max(dt, 0.0f));
    const glm::vec3 blended =
        forward + (glm::normalize(moving) - forward) * alpha;
    if (glm::dot(blended, blended) > 1.0e-8f) {
      forward = glm::normalize(blended);
    }
  }
  avatar.forward = forward;
}

glm::quat remote_avatar_orientation(const RemoteAvatarState &avatar,
                                    const glm::dvec3 &planet_center) {
  const glm::vec3 up = radial_up(avatar.feet, planet_center);
  glm::vec3 forward = tangent(avatar.forward, up);
  forward = glm::dot(forward, forward) > 1.0e-8f ? glm::normalize(forward)
                                                 : any_tangent(up);
  const glm::vec3 side = glm::normalize(glm::cross(up, forward));
  return glm::quat_cast(glm::mat3(side, up, glm::cross(side, up)));
}
