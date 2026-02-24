#pragma once

#include "engine_gameplay/player/player_components.hpp"
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
    RenderMesh build_render_mesh(
        PlayerAnimState state,
        float anim_phase,
        float anim_blend,
        const glm::vec3 &world_position,
        const glm::quat &world_rotation,
        const glm::vec3 &color) const;

private:
    struct VertexBind {
        glm::vec3 position = glm::vec3(0.0f);
        glm::uvec4 joints = glm::uvec4(0u);
        glm::vec4 weights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
    };

    struct NodeTransform {
        int parent = -1;
        glm::vec3 translation = glm::vec3(0.0f);
        glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        glm::vec3 scale = glm::vec3(1.0f);
        std::vector<int> children;
    };

    struct Joint {
        int node_index = -1;
        glm::mat4 inverse_bind = glm::mat4(1.0f);
    };

    struct Vec3Channel {
        int node_index = -1;
        std::vector<float> times;
        std::vector<glm::vec3> values;
    };

    struct QuatChannel {
        int node_index = -1;
        std::vector<float> times;
        std::vector<glm::quat> values;
    };

    struct AnimationClip {
        std::string name;
        std::vector<Vec3Channel> translations;
        std::vector<QuatChannel> rotations;
        std::vector<Vec3Channel> scales;
        float duration = 0.0f;
    };

    int select_clip(PlayerAnimState state) const;
    static glm::vec3 sample_vec3_channel(const Vec3Channel &channel, float time_s);
    static glm::quat sample_quat_channel(const QuatChannel &channel, float time_s);
    static glm::mat4 compose_trs(const glm::vec3 &t, const glm::quat &r, const glm::vec3 &s);
    void compute_global_matrices(
        const std::vector<glm::vec3> &local_t,
        const std::vector<glm::quat> &local_r,
        const std::vector<glm::vec3> &local_s,
        std::vector<glm::mat4> &out_global) const;

    bool ready = false;
    float model_scale = 1.0f;
    float model_ground_lift = 0.0f;
    glm::quat model_axis_correction = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    glm::quat model_facing_correction = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    std::vector<VertexBind> bind_vertices;
    std::vector<uint32_t> mesh_indices;
    std::vector<NodeTransform> nodes;
    std::vector<Joint> joints;
    std::vector<AnimationClip> clips;
};
