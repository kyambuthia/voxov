#pragma once

#include "engine_gameplay/animation/animation_runtime.hpp"
#include "engine_render/render_types.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <string>
#include <vector>

class SkinnedModel {
public:
    bool load_from_glb(const std::string &path, std::string &out_error);
    bool loaded() const;
    bool has_animation() const;
    const AnimationSkeleton &skeleton() const;
    const std::vector<AnimationClip> &clips() const;
    const ResolvedPlayerAnimationGraph &graph() const;
    RenderMesh build_render_mesh(
        const PlayerAnimationRuntime &runtime,
        const glm::vec3 &world_position,
        const glm::quat &world_rotation,
        const glm::vec3 &color) const;
    RenderMesh build_render_mesh(
        PlayerAnimState state,
        float anim_phase,
        PlayerAnimState source_state,
        float source_phase,
        float transition_alpha,
        const glm::vec3 &world_position,
        const glm::quat &world_rotation,
        const glm::vec3 &color) const;
    void append_debug_skeleton(
        RenderMesh &dst,
        const PlayerAnimationRuntime &runtime,
        const glm::vec3 &world_position,
        const glm::quat &world_rotation,
        const glm::vec3 &color,
        float line_thickness) const;

private:
    struct VertexBind {
        glm::vec3 position = glm::vec3(0.0f);
        glm::uvec4 joints = glm::uvec4(0u);
        glm::vec4 weights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
    };

    PlayerAnimationSample sample_pose(
        PlayerAnimState state,
        float anim_phase,
        PlayerAnimState source_state,
        float source_phase,
        float transition_alpha) const;

    bool ready = false;
    float model_scale = 1.0f;
    float model_ground_lift = 0.0f;
    glm::quat model_axis_correction = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    glm::quat model_facing_correction = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    std::vector<VertexBind> bind_vertices;
    std::vector<uint32_t> mesh_indices;
    AnimationSkeleton animation_skeleton;
    std::vector<AnimationClip> animation_clips;
    ResolvedPlayerAnimationGraph animation_graph;
};
