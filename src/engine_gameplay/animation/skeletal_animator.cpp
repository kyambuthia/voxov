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
    pose.local_joints[J(SkeletonJoint::Spine)] = glm::vec3(0.0f, 1.20f, 0.01f);
    pose.local_joints[J(SkeletonJoint::Chest)] = glm::vec3(0.0f, 1.45f, 0.03f);
    pose.local_joints[J(SkeletonJoint::Head)] = glm::vec3(0.0f, 1.82f, 0.06f);

    pose.local_joints[J(SkeletonJoint::ShoulderL)] = glm::vec3(-0.28f, 1.44f, 0.03f);
    pose.local_joints[J(SkeletonJoint::ElbowL)] = glm::vec3(-0.47f, 1.18f, -0.03f);
    pose.local_joints[J(SkeletonJoint::HandL)] = glm::vec3(-0.56f, 0.96f, 0.07f);

    pose.local_joints[J(SkeletonJoint::ShoulderR)] = glm::vec3(0.28f, 1.44f, 0.03f);
    pose.local_joints[J(SkeletonJoint::ElbowR)] = glm::vec3(0.47f, 1.18f, -0.03f);
    pose.local_joints[J(SkeletonJoint::HandR)] = glm::vec3(0.56f, 0.96f, 0.07f);

    pose.local_joints[J(SkeletonJoint::HipL)] = glm::vec3(-0.18f, 0.95f, 0.0f);
    pose.local_joints[J(SkeletonJoint::KneeL)] = glm::vec3(-0.18f, 0.52f, 0.0f);
    pose.local_joints[J(SkeletonJoint::FootL)] = glm::vec3(-0.18f, 0.10f, 0.06f);

    pose.local_joints[J(SkeletonJoint::HipR)] = glm::vec3(0.18f, 0.95f, 0.0f);
    pose.local_joints[J(SkeletonJoint::KneeR)] = glm::vec3(0.18f, 0.52f, 0.0f);
    pose.local_joints[J(SkeletonJoint::FootR)] = glm::vec3(0.18f, 0.10f, 0.06f);
}

void apply_idle_motion(SkeletonPose &pose, float phase, float blend) {
    const float s = std::sin(phase);
    const float c = std::cos(phase * 0.5f);
    const float w = 0.2f + 0.8f * std::clamp(blend, 0.0f, 1.0f);

    pose.local_joints[J(SkeletonJoint::Pelvis)].y += 0.012f * c * w;
    pose.local_joints[J(SkeletonJoint::Pelvis)].x += 0.010f * s * w;
    pose.local_joints[J(SkeletonJoint::Spine)].x += 0.016f * s * w;
    pose.local_joints[J(SkeletonJoint::Chest)].x -= 0.018f * s * w;
    pose.local_joints[J(SkeletonJoint::Head)].x -= 0.022f * s * w;
    pose.local_joints[J(SkeletonJoint::Head)].y += 0.015f * c * w;
    pose.local_joints[J(SkeletonJoint::Head)].z += 0.01f * std::sin(phase * 0.7f) * w;

    pose.local_joints[J(SkeletonJoint::ElbowL)].x -= 0.01f * s * w;
    pose.local_joints[J(SkeletonJoint::ElbowR)].x -= 0.01f * s * w;
    pose.local_joints[J(SkeletonJoint::HandL)].z += 0.02f * std::sin(phase * 1.4f) * w;
    pose.local_joints[J(SkeletonJoint::HandR)].z -= 0.02f * std::sin(phase * 1.4f) * w;
}

void apply_walk_or_run(SkeletonPose &pose, float phase, float stride_scale) {
    const float s = std::sin(phase);
    const float c = std::cos(phase);
    const float s2 = std::sin(phase * 2.0f);
    const float c2 = std::cos(phase * 2.0f);
    const float stride = 0.24f * stride_scale;
    const float lift = 0.14f * stride_scale;
    const float arm = 0.24f * stride_scale;
    const float sway = 0.048f * stride_scale;
    const float twist = 0.07f * stride_scale;
    const float drop_l = std::max(0.0f, -s);
    const float drop_r = std::max(0.0f, s);

    pose.local_joints[J(SkeletonJoint::Pelvis)].x += sway * s;
    pose.local_joints[J(SkeletonJoint::Pelvis)].y -= 0.028f * std::fabs(s);
    pose.local_joints[J(SkeletonJoint::Pelvis)].z += 0.022f * c;
    pose.local_joints[J(SkeletonJoint::Spine)].x -= sway * 0.45f * s;
    pose.local_joints[J(SkeletonJoint::Chest)].x -= sway * 0.8f * s;
    pose.local_joints[J(SkeletonJoint::Chest)].z += twist * s;
    pose.local_joints[J(SkeletonJoint::Head)].x -= sway * 1.0f * s;
    pose.local_joints[J(SkeletonJoint::Head)].z += twist * 0.6f * s;
    pose.local_joints[J(SkeletonJoint::Head)].y += 0.009f * c2;

    pose.local_joints[J(SkeletonJoint::HipL)].x += sway * 0.7f * s;
    pose.local_joints[J(SkeletonJoint::HipR)].x -= sway * 0.7f * s;
    pose.local_joints[J(SkeletonJoint::HipL)].z += 0.045f * s;
    pose.local_joints[J(SkeletonJoint::HipR)].z -= 0.045f * s;
    pose.local_joints[J(SkeletonJoint::ShoulderL)].x -= sway * 0.95f * s;
    pose.local_joints[J(SkeletonJoint::ShoulderR)].x += sway * 0.95f * s;
    pose.local_joints[J(SkeletonJoint::ShoulderL)].z -= 0.04f * s;
    pose.local_joints[J(SkeletonJoint::ShoulderR)].z += 0.04f * s;

    pose.local_joints[J(SkeletonJoint::FootL)].z += stride * s;
    pose.local_joints[J(SkeletonJoint::FootR)].z -= stride * s;
    pose.local_joints[J(SkeletonJoint::FootL)].y += drop_l * lift;
    pose.local_joints[J(SkeletonJoint::FootR)].y += drop_r * lift;
    pose.local_joints[J(SkeletonJoint::FootL)].x += sway * 0.45f * s;
    pose.local_joints[J(SkeletonJoint::FootR)].x -= sway * 0.45f * s;

    pose.local_joints[J(SkeletonJoint::KneeL)].z += (0.155f + 0.025f * c) * s;
    pose.local_joints[J(SkeletonJoint::KneeR)].z -= (0.155f + 0.025f * c) * s;
    pose.local_joints[J(SkeletonJoint::KneeL)].y += drop_l * (lift * 0.95f);
    pose.local_joints[J(SkeletonJoint::KneeR)].y += drop_r * (lift * 0.95f);
    pose.local_joints[J(SkeletonJoint::KneeL)].x += sway * 0.35f * s;
    pose.local_joints[J(SkeletonJoint::KneeR)].x -= sway * 0.35f * s;

    pose.local_joints[J(SkeletonJoint::HandL)].z -= arm * s;
    pose.local_joints[J(SkeletonJoint::HandR)].z += arm * s;
    pose.local_joints[J(SkeletonJoint::HandL)].y += 0.055f * drop_r;
    pose.local_joints[J(SkeletonJoint::HandR)].y += 0.055f * drop_l;
    pose.local_joints[J(SkeletonJoint::HandL)].z += 0.018f * c2;
    pose.local_joints[J(SkeletonJoint::HandR)].z += 0.018f * c2;
    pose.local_joints[J(SkeletonJoint::HandL)].x -= sway * 0.35f * s;
    pose.local_joints[J(SkeletonJoint::HandR)].x += sway * 0.35f * s;
    pose.local_joints[J(SkeletonJoint::ElbowL)].z -= arm * 0.95f * s;
    pose.local_joints[J(SkeletonJoint::ElbowR)].z += arm * 0.95f * s;
    pose.local_joints[J(SkeletonJoint::ElbowL)].y += 0.034f * drop_r;
    pose.local_joints[J(SkeletonJoint::ElbowR)].y += 0.034f * drop_l;

    pose.local_joints[J(SkeletonJoint::Chest)].y += 0.024f * c;
    pose.local_joints[J(SkeletonJoint::Head)].y += 0.016f * c + 0.008f * s2;
}

void apply_jump(SkeletonPose &pose, float phase) {
    const float t = std::sin(phase * 0.5f);
    const float rise = 0.21f + 0.05f * t;
    const float stretch = std::max(0.0f, t);

    for (glm::vec3 &joint : pose.local_joints) {
        joint.y += rise;
    }

    const float tuck = 0.16f + 0.10f * std::max(0.0f, -t);
    pose.local_joints[J(SkeletonJoint::Pelvis)].z -= 0.05f;
    pose.local_joints[J(SkeletonJoint::Chest)].z += 0.035f;
    pose.local_joints[J(SkeletonJoint::Head)].z += 0.05f;

    pose.local_joints[J(SkeletonJoint::KneeL)].y += 0.10f + tuck * 0.35f;
    pose.local_joints[J(SkeletonJoint::KneeR)].y += 0.10f + tuck * 0.35f;
    pose.local_joints[J(SkeletonJoint::KneeL)].z += tuck;
    pose.local_joints[J(SkeletonJoint::KneeR)].z += tuck;
    pose.local_joints[J(SkeletonJoint::FootL)].y += 0.08f + tuck * 0.25f;
    pose.local_joints[J(SkeletonJoint::FootR)].y += 0.08f + tuck * 0.25f;
    pose.local_joints[J(SkeletonJoint::FootL)].z += tuck * 0.45f;
    pose.local_joints[J(SkeletonJoint::FootR)].z += tuck * 0.45f;
    pose.local_joints[J(SkeletonJoint::FootL)].z -= 0.06f * stretch;
    pose.local_joints[J(SkeletonJoint::FootR)].z -= 0.06f * stretch;

    pose.local_joints[J(SkeletonJoint::HandL)].y += 0.17f;
    pose.local_joints[J(SkeletonJoint::HandR)].y += 0.17f;
    pose.local_joints[J(SkeletonJoint::HandL)].z -= 0.13f;
    pose.local_joints[J(SkeletonJoint::HandR)].z -= 0.13f;
    pose.local_joints[J(SkeletonJoint::ElbowL)].z -= 0.09f;
    pose.local_joints[J(SkeletonJoint::ElbowR)].z -= 0.09f;
}

void apply_crawl(SkeletonPose &pose, float phase) {
    const float s = std::sin(phase);
    const float c = std::cos(phase);
    for (glm::vec3 &joint : pose.local_joints) {
        joint.y *= 0.48f;
        joint.z += 0.18f;
    }

    pose.local_joints[J(SkeletonJoint::Pelvis)].y -= 0.05f;
    pose.local_joints[J(SkeletonJoint::Spine)].z += 0.14f;
    pose.local_joints[J(SkeletonJoint::Chest)].z += 0.22f;
    pose.local_joints[J(SkeletonJoint::Head)].z += 0.32f;
    pose.local_joints[J(SkeletonJoint::Head)].y += 0.03f;

    pose.local_joints[J(SkeletonJoint::HandL)].z += 0.30f + 0.10f * s;
    pose.local_joints[J(SkeletonJoint::HandR)].z += 0.30f - 0.10f * s;
    pose.local_joints[J(SkeletonJoint::ElbowL)].z += 0.15f + 0.065f * s;
    pose.local_joints[J(SkeletonJoint::ElbowR)].z += 0.15f - 0.065f * s;
    pose.local_joints[J(SkeletonJoint::HandL)].y += 0.05f * std::max(0.0f, s);
    pose.local_joints[J(SkeletonJoint::HandR)].y += 0.05f * std::max(0.0f, -s);

    pose.local_joints[J(SkeletonJoint::FootL)].z += -0.24f - 0.08f * s;
    pose.local_joints[J(SkeletonJoint::FootR)].z += -0.24f + 0.08f * s;
    pose.local_joints[J(SkeletonJoint::KneeL)].z += -0.19f - 0.04f * c;
    pose.local_joints[J(SkeletonJoint::KneeR)].z += -0.19f + 0.04f * c;
    pose.local_joints[J(SkeletonJoint::KneeL)].y += 0.05f * std::max(0.0f, -s);
    pose.local_joints[J(SkeletonJoint::KneeR)].y += 0.05f * std::max(0.0f, s);
    pose.local_joints[J(SkeletonJoint::Pelvis)].x += 0.022f * s;
    pose.local_joints[J(SkeletonJoint::Chest)].x -= 0.03f * s;
    pose.local_joints[J(SkeletonJoint::Head)].x -= 0.038f * s;
}

glm::mat3 block_frame(const glm::vec3 &a, const glm::vec3 &b) {
    const glm::vec3 delta = b - a;
    if (glm::dot(delta, delta) < 0.000001f) {
        return glm::mat3(1.0f);
    }
    const glm::vec3 up = glm::normalize(delta);
    const glm::vec3 reference = std::fabs(up.z) < 0.95f
        ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 right = glm::normalize(glm::cross(up, reference));
    return glm::mat3(right, up, glm::cross(right, up));
}

void append_block(RenderMesh &dst, const glm::vec3 &center,
                  const glm::vec3 &half, const glm::mat3 &frame,
                  const glm::vec3 &color) {
    const glm::vec3 corners[8] = {
        {-half.x, -half.y, -half.z}, {half.x, -half.y, -half.z},
        {-half.x, half.y, -half.z}, {half.x, half.y, -half.z},
        {-half.x, -half.y, half.z}, {half.x, -half.y, half.z},
        {-half.x, half.y, half.z}, {half.x, half.y, half.z}
    };
    constexpr uint32_t faces[6][4] = {
        {0, 2, 3, 1}, {4, 5, 7, 6}, {0, 4, 6, 2},
        {1, 3, 7, 5}, {2, 6, 7, 3}, {0, 1, 5, 4}
    };
    for (const auto &face : faces) {
        const glm::vec3 normal = glm::normalize(frame * glm::cross(
            corners[face[1]] - corners[face[0]],
            corners[face[2]] - corners[face[0]]));
        const uint32_t base = static_cast<uint32_t>(dst.vertices.size());
        for (uint32_t index : face) {
            dst.vertices.push_back({center + frame * corners[index], color, normal});
        }
        dst.indices.insert(dst.indices.end(),
                           {base, base + 1, base + 2, base, base + 2, base + 3});
    }
}

void append_joint(RenderMesh &dst, const glm::vec3 &p, float r, const glm::vec3 &color) {
    append_mesh(dst, build_debug_sphere_mesh(p, r, color));
}
}

SkeletonPose SkeletalAnimator::sample_pose(PlayerAnimState state, float phase, float blend) {
    SkeletonPose pose{};
    apply_standing_pose(pose);
    const float clamped_blend = std::clamp(blend, 0.0f, 1.0f);
    apply_idle_motion(pose, phase, clamped_blend);

    if (player_anim_is_walk_cycle(state) ||
        state == PlayerAnimState::StartMove ||
        state == PlayerAnimState::PivotLeft ||
        state == PlayerAnimState::PivotRight ||
        state == PlayerAnimState::TurnInPlaceLeft ||
        state == PlayerAnimState::TurnInPlaceRight ||
        state == PlayerAnimState::MovingTurn) {
        apply_walk_or_run(pose, phase, 0.82f + 0.24f * clamped_blend);
    } else if (player_anim_is_run_cycle(state)) {
        apply_walk_or_run(pose, phase, 1.15f + 0.34f * clamped_blend);
    } else if (state == PlayerAnimState::JumpTakeoff ||
               state == PlayerAnimState::JumpLoop ||
               state == PlayerAnimState::FallLoop ||
               state == PlayerAnimState::LandSoft ||
               state == PlayerAnimState::LandHard) {
        apply_jump(pose, phase);
    } else if (state == PlayerAnimState::StopMove ||
               state == PlayerAnimState::Recovery) {
        apply_crawl(pose, phase);
    } else {
        // Idle keeps standing pose.
    }

    return pose;
}

void SkeletalAnimator::append_blocky_mesh(
    RenderMesh &dst,
    const SkeletonPose &pose,
    const glm::vec3 &feet_position,
    const glm::quat &orientation,
    const glm::vec3 &color) {
    const glm::mat3 frame = glm::mat3_cast(orientation);
    const auto world = [&](SkeletonJoint joint) {
        return feet_position + frame * pose.local_joints[J(joint)];
    };
    const glm::vec3 forward = frame * glm::vec3(0.0f, 0.0f, 1.0f);
    const glm::vec3 right_axis = frame * glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 up_axis = frame * glm::vec3(0.0f, 1.0f, 0.0f);

    const glm::vec3 jacket = color * glm::vec3(0.92f, 0.94f, 0.96f);
    const glm::vec3 jacket_trim = color * glm::vec3(0.68f, 0.76f, 0.88f);
    const glm::vec3 trousers = glm::vec3(0.24f, 0.28f, 0.36f);
    const glm::vec3 boots = glm::vec3(0.14f, 0.15f, 0.19f);
    const glm::vec3 accent = color * glm::vec3(1.0f, 0.82f, 0.42f);
    const glm::vec3 skin = glm::vec3(0.92f, 0.76f, 0.62f);
    const glm::vec3 visor = glm::vec3(0.12f, 0.18f, 0.26f);

    const glm::vec3 pelvis = world(SkeletonJoint::Pelvis);
    const glm::vec3 chest = world(SkeletonJoint::Chest);
    const glm::vec3 head = world(SkeletonJoint::Head);
    const glm::vec3 spine_mid = 0.5f * (pelvis + chest);
    const glm::vec3 neck = 0.5f * (chest + head) + up_axis * 0.02f;

    const glm::vec3 shoulder_l = world(SkeletonJoint::ShoulderL);
    const glm::vec3 elbow_l = world(SkeletonJoint::ElbowL);
    const glm::vec3 hand_l = world(SkeletonJoint::HandL);
    const glm::vec3 shoulder_r = world(SkeletonJoint::ShoulderR);
    const glm::vec3 elbow_r = world(SkeletonJoint::ElbowR);
    const glm::vec3 hand_r = world(SkeletonJoint::HandR);
    const glm::vec3 hip_l = world(SkeletonJoint::HipL);
    const glm::vec3 knee_l = world(SkeletonJoint::KneeL);
    const glm::vec3 foot_l = world(SkeletonJoint::FootL);
    const glm::vec3 hip_r = world(SkeletonJoint::HipR);
    const glm::vec3 knee_r = world(SkeletonJoint::KneeR);
    const glm::vec3 foot_r = world(SkeletonJoint::FootR);

    // Pelvis + belt + torso + jacket chest
    append_block(dst, pelvis, glm::vec3(0.19f, 0.10f, 0.125f), frame, trousers);
    append_block(dst, pelvis + up_axis * 0.085f, glm::vec3(0.205f, 0.032f, 0.135f), frame, boots);
    append_block(dst, pelvis + up_axis * 0.085f + forward * 0.138f, glm::vec3(0.045f, 0.028f, 0.015f), frame, accent);
    append_block(dst, spine_mid, glm::vec3(0.20f, 0.20f, 0.135f), frame, jacket);
    append_block(dst, chest, glm::vec3(0.23f, 0.16f, 0.15f), frame, jacket);
    append_block(dst, chest + up_axis * 0.15f, glm::vec3(0.14f, 0.03f, 0.12f), frame, jacket_trim);

    // Neck + head + recessed visor/helmet cap
    append_block(dst, neck, glm::vec3(0.065f, 0.075f, 0.065f), frame, skin);
    append_block(dst, head, glm::vec3(0.135f, 0.145f, 0.135f), frame, skin);
    append_block(dst, head + up_axis * 0.10f, glm::vec3(0.145f, 0.06f, 0.145f), frame, jacket_trim);
    append_block(dst, head + forward * 0.132f + up_axis * 0.015f, glm::vec3(0.105f, 0.045f, 0.022f), frame, visor);
    append_block(dst, head + forward * 0.138f + up_axis * 0.02f - right_axis * 0.055f, glm::vec3(0.022f, 0.018f, 0.012f), frame, accent);
    append_block(dst, head + forward * 0.138f + up_axis * 0.02f + right_axis * 0.055f, glm::vec3(0.022f, 0.018f, 0.012f), frame, accent);

    // Explorer backpack
    const glm::vec3 pack_center = chest - forward * 0.175f - up_axis * 0.02f;
    append_block(dst, pack_center, glm::vec3(0.145f, 0.18f, 0.065f), frame, jacket_trim);
    append_block(dst, pack_center - forward * 0.062f + up_axis * 0.08f, glm::vec3(0.11f, 0.03f, 0.015f), frame, accent);

    const auto append_limb = [&](const glm::vec3 &a, const glm::vec3 &b,
                                 float thickness, const glm::vec3 &limb_color) {
        const glm::mat3 limb_frame = block_frame(a, b);
        const glm::vec3 mid = 0.5f * (a + b);
        const glm::vec3 half(thickness, 0.5f * glm::length(b - a) + 0.015f, thickness);
        append_block(dst, mid, half, limb_frame, limb_color);
    };

    // Arms + shoulder pads + gloves
    append_block(dst, shoulder_l, glm::vec3(0.072f, 0.06f, 0.072f), frame, jacket_trim);
    append_limb(shoulder_l, elbow_l, 0.056f, jacket);
    append_limb(elbow_l, hand_l, 0.048f, jacket_trim);
    append_block(dst, hand_l, glm::vec3(0.056f), block_frame(elbow_l, hand_l), accent);

    append_block(dst, shoulder_r, glm::vec3(0.072f, 0.06f, 0.072f), frame, jacket_trim);
    append_limb(shoulder_r, elbow_r, 0.056f, jacket);
    append_limb(elbow_r, hand_r, 0.048f, jacket_trim);
    append_block(dst, hand_r, glm::vec3(0.056f), block_frame(elbow_r, hand_r), accent);

    // Legs + boots
    append_limb(hip_l, knee_l, 0.072f, trousers);
    append_limb(knee_l, foot_l, 0.062f, trousers);
    append_block(dst, foot_l + up_axis * 0.04f + forward * 0.025f,
                 glm::vec3(0.082f, 0.048f, 0.125f), frame, boots);

    append_limb(hip_r, knee_r, 0.072f, trousers);
    append_limb(knee_r, foot_r, 0.062f, trousers);
    append_block(dst, foot_r + up_axis * 0.04f + forward * 0.025f,
                 glm::vec3(0.082f, 0.048f, 0.125f), frame, boots);
}

void SkeletalAnimator::append_debug_skeleton(
    RenderMesh &dst,
    const SkeletonPose &pose,
    const glm::vec3 &feet_position,
    const glm::quat &orientation,
    const glm::vec3 &color,
    float line_thickness) {
    const glm::mat3 frame = glm::mat3_cast(orientation);
    const glm::vec3 forward = frame * glm::vec3(0.0f, 0.0f, 1.0f);
    const glm::vec3 right_axis = frame * glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 up_axis = frame * glm::vec3(0.0f, 1.0f, 0.0f);

    std::array<glm::vec3, static_cast<size_t>(SkeletonJoint::Count)> world{};
    for (size_t i = 0; i < world.size(); ++i) {
        world[i] = feet_position + orientation * pose.local_joints[i];
    }

    const glm::vec3 bone_shade = color * glm::vec3(0.86f, 0.84f, 0.78f);
    const glm::vec3 socket_dark(0.10f, 0.11f, 0.14f);
    const float r_bone = std::max(0.024f, line_thickness * 0.78f);

    const auto append_bone_seg = [&](SkeletonJoint a, SkeletonJoint b, float radius) {
        const glm::vec3 pa = world[J(a)];
        const glm::vec3 pb = world[J(b)];
        const glm::mat3 bf = block_frame(pa, pb);
        const glm::vec3 mid = 0.5f * (pa + pb);
        const float half_len = 0.5f * glm::length(pb - pa);
        append_block(dst, mid, glm::vec3(radius, half_len, radius), bf, color);
    };

    // Skull + jaw + eye sockets
    const glm::vec3 head = world[J(SkeletonJoint::Head)];
    append_block(dst, head + up_axis * 0.02f, glm::vec3(0.125f, 0.12f, 0.125f), frame, color);
    append_block(dst, head - up_axis * 0.09f + forward * 0.03f, glm::vec3(0.085f, 0.04f, 0.095f), frame, bone_shade);
    append_block(dst, head + forward * 0.122f + up_axis * 0.02f - right_axis * 0.048f, glm::vec3(0.026f, 0.026f, 0.014f), frame, socket_dark);
    append_block(dst, head + forward * 0.122f + up_axis * 0.02f + right_axis * 0.048f, glm::vec3(0.026f, 0.026f, 0.014f), frame, socket_dark);

    // Spine + ribcage + pelvis
    append_bone_seg(SkeletonJoint::Pelvis, SkeletonJoint::Spine, r_bone * 1.05f);
    append_bone_seg(SkeletonJoint::Spine, SkeletonJoint::Chest, r_bone * 1.05f);
    append_bone_seg(SkeletonJoint::Chest, SkeletonJoint::Head, r_bone * 0.90f);

    const glm::vec3 chest = world[J(SkeletonJoint::Chest)];
    const glm::vec3 spine = world[J(SkeletonJoint::Spine)];
    const glm::vec3 pelvis = world[J(SkeletonJoint::Pelvis)];
    for (int r = 0; r < 3; ++r) {
        const float t = static_cast<float>(r) * 0.35f;
        const glm::vec3 rib_pos = glm::mix(chest, spine, t);
        const float rib_w = 0.165f - 0.018f * static_cast<float>(r);
        append_block(dst, rib_pos, glm::vec3(rib_w, 0.022f, 0.105f), frame, bone_shade);
    }
    append_block(dst, pelvis, glm::vec3(0.175f, 0.055f, 0.095f), frame, bone_shade);

    // Collarbone + arms
    append_bone_seg(SkeletonJoint::Chest, SkeletonJoint::ShoulderL, r_bone * 0.85f);
    append_bone_seg(SkeletonJoint::ShoulderL, SkeletonJoint::ElbowL, r_bone * 0.85f);
    append_bone_seg(SkeletonJoint::ElbowL, SkeletonJoint::HandL, r_bone * 0.72f);

    append_bone_seg(SkeletonJoint::Chest, SkeletonJoint::ShoulderR, r_bone * 0.85f);
    append_bone_seg(SkeletonJoint::ShoulderR, SkeletonJoint::ElbowR, r_bone * 0.85f);
    append_bone_seg(SkeletonJoint::ElbowR, SkeletonJoint::HandR, r_bone * 0.72f);

    // Hips + legs + bony feet
    append_bone_seg(SkeletonJoint::Pelvis, SkeletonJoint::HipL, r_bone);
    append_bone_seg(SkeletonJoint::HipL, SkeletonJoint::KneeL, r_bone);
    append_bone_seg(SkeletonJoint::KneeL, SkeletonJoint::FootL, r_bone * 0.85f);
    append_block(dst, world[J(SkeletonJoint::FootL)] + up_axis * 0.025f + forward * 0.03f,
                 glm::vec3(0.052f, 0.024f, 0.095f), frame, bone_shade);

    append_bone_seg(SkeletonJoint::Pelvis, SkeletonJoint::HipR, r_bone);
    append_bone_seg(SkeletonJoint::HipR, SkeletonJoint::KneeR, r_bone);
    append_bone_seg(SkeletonJoint::KneeR, SkeletonJoint::FootR, r_bone * 0.85f);
    append_block(dst, world[J(SkeletonJoint::FootR)] + up_axis * 0.025f + forward * 0.03f,
                 glm::vec3(0.052f, 0.024f, 0.095f), frame, bone_shade);

    const float jr = r_bone * 1.25f;
    append_joint(dst, world[J(SkeletonJoint::ShoulderL)], jr, bone_shade);
    append_joint(dst, world[J(SkeletonJoint::ElbowL)], jr * 0.9f, bone_shade);
    append_joint(dst, world[J(SkeletonJoint::HandL)], jr * 0.8f, color);
    append_joint(dst, world[J(SkeletonJoint::ShoulderR)], jr, bone_shade);
    append_joint(dst, world[J(SkeletonJoint::ElbowR)], jr * 0.9f, bone_shade);
    append_joint(dst, world[J(SkeletonJoint::HandR)], jr * 0.8f, color);
    append_joint(dst, world[J(SkeletonJoint::HipL)], jr, bone_shade);
    append_joint(dst, world[J(SkeletonJoint::KneeL)], jr * 0.95f, bone_shade);
    append_joint(dst, world[J(SkeletonJoint::HipR)], jr, bone_shade);
    append_joint(dst, world[J(SkeletonJoint::KneeR)], jr * 0.95f, bone_shade);
}
