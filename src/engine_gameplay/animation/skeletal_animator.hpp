#pragma once

#include "engine_gameplay/player/player_components.hpp"
#include "engine_render/render_types.hpp"

#include <array>
#include <cstdint>

#include <glm/glm.hpp>

enum class SkeletonJoint : uint8_t {
    Pelvis = 0,
    Spine,
    Chest,
    Head,
    ShoulderL,
    ElbowL,
    HandL,
    ShoulderR,
    ElbowR,
    HandR,
    HipL,
    KneeL,
    FootL,
    HipR,
    KneeR,
    FootR,
    Count
};

struct SkeletonPose {
    std::array<glm::vec3, static_cast<size_t>(SkeletonJoint::Count)> local_joints{};
};

class SkeletalAnimator {
public:
    static SkeletonPose sample_pose(PlayerAnimState state, float phase, float blend);
    static void append_debug_skeleton(
        RenderMesh &dst,
        const SkeletonPose &pose,
        const glm::vec3 &feet_position,
        const glm::quat &orientation,
        const glm::vec3 &color,
        float line_thickness);
};

