#include "engine_gameplay/animation/skeletal_animator.hpp"

#include "engine_render/debug_draw/debug_draw.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace {
constexpr size_t J(SkeletonJoint joint) {
    return static_cast<size_t>(joint);
}

void apply_standing_pose(SkeletonPose &pose) {
    pose.local_joints[J(SkeletonJoint::Pelvis)] = glm::vec3(0.0f, 0.95f, 0.0f);
    pose.local_joints[J(SkeletonJoint::Spine)] = glm::vec3(0.0f, 1.18f, 0.0f);
    pose.local_joints[J(SkeletonJoint::Chest)] = glm::vec3(0.0f, 1.42f, 0.0f);
    pose.local_joints[J(SkeletonJoint::Head)] = glm::vec3(0.0f, 1.80f, 0.0f);

    pose.local_joints[J(SkeletonJoint::ShoulderL)] = glm::vec3(-0.26f, 1.42f, 0.0f);
    pose.local_joints[J(SkeletonJoint::ElbowL)] = glm::vec3(-0.42f, 1.18f, 0.0f);
    pose.local_joints[J(SkeletonJoint::HandL)] = glm::vec3(-0.52f, 0.96f, 0.02f);

    pose.local_joints[J(SkeletonJoint::ShoulderR)] = glm::vec3(0.26f, 1.42f, 0.0f);
    pose.local_joints[J(SkeletonJoint::ElbowR)] = glm::vec3(0.42f, 1.18f, 0.0f);
    pose.local_joints[J(SkeletonJoint::HandR)] = glm::vec3(0.52f, 0.96f, 0.02f);

    pose.local_joints[J(SkeletonJoint::HipL)] = glm::vec3(-0.18f, 0.95f, 0.0f);
    pose.local_joints[J(SkeletonJoint::KneeL)] = glm::vec3(-0.18f, 0.52f, 0.0f);
    pose.local_joints[J(SkeletonJoint::FootL)] = glm::vec3(-0.18f, 0.10f, 0.06f);

    pose.local_joints[J(SkeletonJoint::HipR)] = glm::vec3(0.18f, 0.95f, 0.0f);
    pose.local_joints[J(SkeletonJoint::KneeR)] = glm::vec3(0.18f, 0.52f, 0.0f);
    pose.local_joints[J(SkeletonJoint::FootR)] = glm::vec3(0.18f, 0.10f, 0.06f);
}

void apply_walk_or_run(SkeletonPose &pose, float phase, float stride_scale) {
    const float s = std::sin(phase);
    const float c = std::cos(phase);
    const float stride = 0.22f * stride_scale;
    const float lift = 0.12f * stride_scale;
    const float arm = 0.18f * stride_scale;

    pose.local_joints[J(SkeletonJoint::FootL)].z += stride * s;
    pose.local_joints[J(SkeletonJoint::FootR)].z -= stride * s;
    pose.local_joints[J(SkeletonJoint::FootL)].y += std::max(0.0f, -s) * lift;
    pose.local_joints[J(SkeletonJoint::FootR)].y += std::max(0.0f, s) * lift;

    pose.local_joints[J(SkeletonJoint::KneeL)].z += 0.13f * s;
    pose.local_joints[J(SkeletonJoint::KneeR)].z -= 0.13f * s;
    pose.local_joints[J(SkeletonJoint::KneeL)].y += std::max(0.0f, -s) * (lift * 0.8f);
    pose.local_joints[J(SkeletonJoint::KneeR)].y += std::max(0.0f, s) * (lift * 0.8f);

    pose.local_joints[J(SkeletonJoint::HandL)].z -= arm * s;
    pose.local_joints[J(SkeletonJoint::HandR)].z += arm * s;
    pose.local_joints[J(SkeletonJoint::ElbowL)].z -= arm * 0.7f * s;
    pose.local_joints[J(SkeletonJoint::ElbowR)].z += arm * 0.7f * s;

    pose.local_joints[J(SkeletonJoint::Chest)].y += 0.02f * c;
    pose.local_joints[J(SkeletonJoint::Head)].y += 0.015f * c;
}

void apply_jump(SkeletonPose &pose, float phase) {
    const float t = std::sin(phase * 0.5f);
    const float rise = 0.22f + 0.06f * t;

    for (glm::vec3 &joint : pose.local_joints) {
        joint.y += rise;
    }

    pose.local_joints[J(SkeletonJoint::KneeL)].y += 0.08f;
    pose.local_joints[J(SkeletonJoint::KneeR)].y += 0.08f;
    pose.local_joints[J(SkeletonJoint::FootL)].y += 0.06f;
    pose.local_joints[J(SkeletonJoint::FootR)].y += 0.06f;

    pose.local_joints[J(SkeletonJoint::HandL)].y += 0.16f;
    pose.local_joints[J(SkeletonJoint::HandR)].y += 0.16f;
    pose.local_joints[J(SkeletonJoint::HandL)].z -= 0.08f;
    pose.local_joints[J(SkeletonJoint::HandR)].z -= 0.08f;
}

void apply_crawl(SkeletonPose &pose, float phase) {
    const float s = std::sin(phase);
    for (glm::vec3 &joint : pose.local_joints) {
        joint.y *= 0.48f;
        joint.z += 0.18f;
    }

    pose.local_joints[J(SkeletonJoint::Head)].z += 0.30f;
    pose.local_joints[J(SkeletonJoint::Chest)].z += 0.20f;
    pose.local_joints[J(SkeletonJoint::HandL)].z += 0.24f + 0.08f * s;
    pose.local_joints[J(SkeletonJoint::HandR)].z += 0.24f - 0.08f * s;
    pose.local_joints[J(SkeletonJoint::FootL)].z += -0.20f - 0.06f * s;
    pose.local_joints[J(SkeletonJoint::FootR)].z += -0.20f + 0.06f * s;
    pose.local_joints[J(SkeletonJoint::KneeL)].z += -0.12f;
    pose.local_joints[J(SkeletonJoint::KneeR)].z += -0.12f;
}

void append_bone(RenderMesh &dst, const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &color, float thickness) {
    append_mesh(dst, build_debug_line_mesh(a, b, thickness, color));
}

void append_joint(RenderMesh &dst, const glm::vec3 &p, float r, const glm::vec3 &color) {
    append_mesh(dst, build_debug_sphere_mesh(p, r, color));
}
}

SkeletonPose SkeletalAnimator::sample_pose(PlayerAnimState state, float phase, float blend) {
    SkeletonPose pose{};
    apply_standing_pose(pose);
    const float clamped_blend = std::clamp(blend, 0.0f, 1.0f);

    switch (state) {
    case PlayerAnimState::Walk:
        apply_walk_or_run(pose, phase, 0.75f + 0.25f * clamped_blend);
        break;
    case PlayerAnimState::Run:
        apply_walk_or_run(pose, phase, 1.1f + 0.35f * clamped_blend);
        break;
    case PlayerAnimState::Jump:
        apply_jump(pose, phase);
        break;
    case PlayerAnimState::Crawl:
        apply_crawl(pose, phase);
        break;
    case PlayerAnimState::Idle:
    default:
        // Idle keeps standing pose.
        break;
    }

    return pose;
}

void SkeletalAnimator::append_debug_skeleton(
    RenderMesh &dst,
    const SkeletonPose &pose,
    const glm::vec3 &feet_position,
    const glm::quat &orientation,
    const glm::vec3 &color,
    float line_thickness) {
    std::array<glm::vec3, static_cast<size_t>(SkeletonJoint::Count)> world{};
    for (size_t i = 0; i < world.size(); ++i) {
        world[i] = feet_position + orientation * pose.local_joints[i];
    }

    append_bone(dst, world[J(SkeletonJoint::Pelvis)], world[J(SkeletonJoint::Spine)], color, line_thickness);
    append_bone(dst, world[J(SkeletonJoint::Spine)], world[J(SkeletonJoint::Chest)], color, line_thickness);
    append_bone(dst, world[J(SkeletonJoint::Chest)], world[J(SkeletonJoint::Head)], color, line_thickness);

    append_bone(dst, world[J(SkeletonJoint::Chest)], world[J(SkeletonJoint::ShoulderL)], color, line_thickness);
    append_bone(dst, world[J(SkeletonJoint::ShoulderL)], world[J(SkeletonJoint::ElbowL)], color, line_thickness);
    append_bone(dst, world[J(SkeletonJoint::ElbowL)], world[J(SkeletonJoint::HandL)], color, line_thickness);

    append_bone(dst, world[J(SkeletonJoint::Chest)], world[J(SkeletonJoint::ShoulderR)], color, line_thickness);
    append_bone(dst, world[J(SkeletonJoint::ShoulderR)], world[J(SkeletonJoint::ElbowR)], color, line_thickness);
    append_bone(dst, world[J(SkeletonJoint::ElbowR)], world[J(SkeletonJoint::HandR)], color, line_thickness);

    append_bone(dst, world[J(SkeletonJoint::Pelvis)], world[J(SkeletonJoint::HipL)], color, line_thickness);
    append_bone(dst, world[J(SkeletonJoint::HipL)], world[J(SkeletonJoint::KneeL)], color, line_thickness);
    append_bone(dst, world[J(SkeletonJoint::KneeL)], world[J(SkeletonJoint::FootL)], color, line_thickness);

    append_bone(dst, world[J(SkeletonJoint::Pelvis)], world[J(SkeletonJoint::HipR)], color, line_thickness);
    append_bone(dst, world[J(SkeletonJoint::HipR)], world[J(SkeletonJoint::KneeR)], color, line_thickness);
    append_bone(dst, world[J(SkeletonJoint::KneeR)], world[J(SkeletonJoint::FootR)], color, line_thickness);
}

void SkeletalAnimator::append_debug_rig_mesh(
    RenderMesh &dst,
    const SkeletonPose &pose,
    const glm::vec3 &feet_position,
    const glm::quat &orientation,
    const glm::vec3 &color) {
    std::array<glm::vec3, static_cast<size_t>(SkeletonJoint::Count)> world{};
    for (size_t i = 0; i < world.size(); ++i) {
        world[i] = feet_position + orientation * pose.local_joints[i];
    }

    const glm::vec3 limb = color * glm::vec3(0.92f, 0.92f, 0.92f);
    const glm::vec3 core = color * glm::vec3(1.06f, 1.06f, 1.06f);

    append_bone(dst, world[J(SkeletonJoint::Pelvis)], world[J(SkeletonJoint::Spine)], core, 0.045f);
    append_bone(dst, world[J(SkeletonJoint::Spine)], world[J(SkeletonJoint::Chest)], core, 0.045f);
    append_bone(dst, world[J(SkeletonJoint::Chest)], world[J(SkeletonJoint::Head)], core, 0.04f);

    append_bone(dst, world[J(SkeletonJoint::Chest)], world[J(SkeletonJoint::ShoulderL)], limb, 0.035f);
    append_bone(dst, world[J(SkeletonJoint::ShoulderL)], world[J(SkeletonJoint::ElbowL)], limb, 0.032f);
    append_bone(dst, world[J(SkeletonJoint::ElbowL)], world[J(SkeletonJoint::HandL)], limb, 0.028f);
    append_bone(dst, world[J(SkeletonJoint::Chest)], world[J(SkeletonJoint::ShoulderR)], limb, 0.035f);
    append_bone(dst, world[J(SkeletonJoint::ShoulderR)], world[J(SkeletonJoint::ElbowR)], limb, 0.032f);
    append_bone(dst, world[J(SkeletonJoint::ElbowR)], world[J(SkeletonJoint::HandR)], limb, 0.028f);

    append_bone(dst, world[J(SkeletonJoint::Pelvis)], world[J(SkeletonJoint::HipL)], limb, 0.04f);
    append_bone(dst, world[J(SkeletonJoint::HipL)], world[J(SkeletonJoint::KneeL)], limb, 0.038f);
    append_bone(dst, world[J(SkeletonJoint::KneeL)], world[J(SkeletonJoint::FootL)], limb, 0.032f);
    append_bone(dst, world[J(SkeletonJoint::Pelvis)], world[J(SkeletonJoint::HipR)], limb, 0.04f);
    append_bone(dst, world[J(SkeletonJoint::HipR)], world[J(SkeletonJoint::KneeR)], limb, 0.038f);
    append_bone(dst, world[J(SkeletonJoint::KneeR)], world[J(SkeletonJoint::FootR)], limb, 0.032f);

    append_joint(dst, world[J(SkeletonJoint::Head)], 0.085f, core);
    append_joint(dst, world[J(SkeletonJoint::Chest)], 0.05f, core);
    append_joint(dst, world[J(SkeletonJoint::Pelvis)], 0.055f, core);
    append_joint(dst, world[J(SkeletonJoint::HandL)], 0.03f, core);
    append_joint(dst, world[J(SkeletonJoint::HandR)], 0.03f, core);
    append_joint(dst, world[J(SkeletonJoint::FootL)], 0.034f, core);
    append_joint(dst, world[J(SkeletonJoint::FootR)], 0.034f, core);
}
