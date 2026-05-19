#include "engine_math/camera.hpp"

#include <glm/gtx/euler_angles.hpp>

void Camera::set_view_override(const glm::mat4 &view_matrix) {
    view_override = view_matrix;
    use_view_override = true;
}

void Camera::clear_view_override() {
    use_view_override = false;
}

bool Camera::has_view_override() const {
    return use_view_override;
}

glm::mat4 Camera::view() const {
    if (use_view_override) {
        return view_override;
    }
    return glm::inverse(transform.matrix());
}

glm::mat4 Camera::projection(float aspect_ratio) const {
    glm::mat4 p = glm::perspective(fov_y_radians, aspect_ratio, z_near, z_far);
    return p;
}

glm::vec3 Camera::forward() const {
    if (use_view_override) {
        const glm::mat4 inv_v = glm::inverse(view_override);
        return glm::normalize(glm::vec3(inv_v * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
    }
    glm::mat4 r = glm::yawPitchRoll(transform.euler_radians.y, transform.euler_radians.x, transform.euler_radians.z);
    return glm::normalize(glm::vec3(r * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
}

glm::vec3 Camera::right() const {
    if (use_view_override) {
        const glm::mat4 inv_v = glm::inverse(view_override);
        return glm::normalize(glm::vec3(inv_v * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)));
    }
    glm::mat4 r = glm::yawPitchRoll(transform.euler_radians.y, transform.euler_radians.x, transform.euler_radians.z);
    return glm::normalize(glm::vec3(r * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)));
}

glm::vec3 Camera::up() const {
    if (use_view_override) {
        const glm::mat4 inv_v = glm::inverse(view_override);
        return glm::normalize(glm::vec3(inv_v * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f)));
    }
    glm::mat4 r = glm::yawPitchRoll(transform.euler_radians.y, transform.euler_radians.x, transform.euler_radians.z);
    return glm::normalize(glm::vec3(r * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f)));
}
