#include "engine_math/camera.hpp"

#include <glm/gtx/euler_angles.hpp>

glm::mat4 Camera::view() const {
    return glm::inverse(transform.matrix());
}

glm::mat4 Camera::projection(float aspect_ratio) const {
    glm::mat4 p = glm::perspective(fov_y_radians, aspect_ratio, z_near, z_far);
    p[1][1] *= -1.0f; // Vulkan clip-space Y inversion.
    return p;
}

glm::vec3 Camera::forward() const {
    glm::mat4 r = glm::yawPitchRoll(transform.euler_radians.y, transform.euler_radians.x, transform.euler_radians.z);
    return glm::normalize(glm::vec3(r * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
}

glm::vec3 Camera::right() const {
    glm::mat4 r = glm::yawPitchRoll(transform.euler_radians.y, transform.euler_radians.x, transform.euler_radians.z);
    return glm::normalize(glm::vec3(r * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)));
}

glm::vec3 Camera::up() const {
    glm::mat4 r = glm::yawPitchRoll(transform.euler_radians.y, transform.euler_radians.x, transform.euler_radians.z);
    return glm::normalize(glm::vec3(r * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f)));
}
