#pragma once

#include <cstdint>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

struct TransformComponent {
    glm::vec3 position = glm::vec3(0.0f);
    glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    glm::vec3 scale = glm::vec3(1.0f);
};

struct CharacterController {
    float capsuleRadius = 0.35f;
    float capsuleHeight = 1.8f;
    float walkSpeed = 4.0f;
    float sprintSpeed = 7.2f;
    float crawlSpeed = 2.2f;
    float jumpVelocity = 5.5f;
    float gravity = -19.62f;
    float maxSlopeDeg = 50.0f;
    bool grounded = false;
    glm::vec3 velocity = glm::vec3(0.0f);
};

enum class PlayerAnimState : uint8_t {
    Idle = 0,
    Walk = 1,
    Run = 2,
    Jump = 3,
    Crawl = 4
};

struct CameraRig {
    float yaw = 180.0f;
    float pitch = -12.0f;
    float distance = 5.0f;
    float minDistance = 1.5f;
    float maxDistance = 8.0f;
    float pivotHeight = 1.5f;
    float sensitivityMouse = 0.11f;
    float sensitivityTouch = 120.0f;
    float pitchMinDeg = -75.0f;
    float pitchMaxDeg = 25.0f;
};

struct PlayerEntity {
    uint32_t network_id = 1;
    TransformComponent transform{};
    CharacterController controller{};
    CameraRig camera_rig{};
    PlayerAnimState anim_state = PlayerAnimState::Idle;
    float anim_phase = 0.0f;
    float anim_blend = 0.0f;
};

struct ReplicatedPlayerMotion {
    uint32_t network_id = 1;
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 velocity = glm::vec3(0.0f);
};
