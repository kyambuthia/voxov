#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>

struct Transform {
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::vec3 euler_radians{0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f, 1.0f, 1.0f};

    glm::mat4 matrix() const {
        glm::mat4 t = glm::translate(glm::mat4(1.0f), position);
        glm::mat4 r = glm::yawPitchRoll(euler_radians.y, euler_radians.x, euler_radians.z);
        glm::mat4 s = glm::scale(glm::mat4(1.0f), scale);
        return t * r * s;
    }
};

class Camera {
public:
    Transform transform;
    float fov_y_radians = glm::radians(70.0f);
    float z_near = 0.1f;
    float z_far = 2000.0f;

    glm::mat4 view() const;
    glm::mat4 projection(float aspect_ratio) const;
    glm::vec3 forward() const;
    glm::vec3 right() const;
    glm::vec3 up() const;
};
