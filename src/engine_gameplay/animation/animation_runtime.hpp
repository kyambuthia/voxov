#pragma once

#include "engine_gameplay/animation/player_animation_graph.hpp"
#include "engine_render/render_types.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <string>
#include <vector>

struct AnimationTransform {
    glm::vec3 translation = glm::vec3(0.0f);
    glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    glm::vec3 scale = glm::vec3(1.0f);
};

struct AnimationSkeletonNode {
    std::string name;
    int parent = -1;
    AnimationTransform bind_local{};
    glm::mat4 inverse_bind = glm::mat4(1.0f);
    bool skin_joint = false;
};

struct AnimationSkeleton {
    std::vector<AnimationSkeletonNode> nodes;
    std::vector<int> skin_joints;
};

struct AnimationVec3Track {
    int node_index = -1;
    std::vector<float> times;
    std::vector<glm::vec3> values;
};

struct AnimationQuatTrack {
    int node_index = -1;
    std::vector<float> times;
    std::vector<glm::quat> values;
};

struct AnimationClip {
    std::string name;
    std::vector<AnimationVec3Track> translations;
    std::vector<AnimationQuatTrack> rotations;
    std::vector<AnimationVec3Track> scales;
    float duration = 0.0f;
};

struct ResolvedPlayerAnimationState {
    PlayerAnimationStateDefinition definition{};
    int clip_index = -1;
    bool placeholder = true;
    std::string resolved_clip_name;
};

struct ResolvedPlayerAnimationGraph {
    std::vector<ResolvedPlayerAnimationState> states;

    const ResolvedPlayerAnimationState *find(PlayerAnimState state) const;
};

struct PlayerAnimationSample {
    PlayerAnimState state = PlayerAnimState::Idle;
    bool placeholder = true;
    std::string clip_name;
    std::vector<AnimationTransform> local_pose;
    std::vector<glm::mat4> global_matrices;
    std::vector<glm::mat4> skin_matrices;
};

struct PlayerAnimationFiredEvent {
    PlayerAnimState state = PlayerAnimState::Idle;
    PlayerAnimEventType type = PlayerAnimEventType::None;
    std::string payload;
};

class PlayerAnimationRuntime {
public:
    void reset(PlayerAnimState state = PlayerAnimState::Idle);
    void request_state(PlayerAnimState state, float crossfade_seconds);
    void advance(float dt);
    void advance_transition(float dt);
    void sync_external_state(PlayerAnimState state, float phase_radians, float crossfade_seconds);

    PlayerAnimState state() const;
    PlayerAnimState previous_state() const;
    float phase_radians() const;
    float previous_phase_radians() const;
    float transition_alpha() const;
    const std::vector<PlayerAnimationFiredEvent> &events() const;
    void clear_events();

private:
    PlayerAnimState current_state = PlayerAnimState::Idle;
    PlayerAnimState source_state = PlayerAnimState::Idle;
    float current_phase = 0.0f;
    float source_phase = 0.0f;
    float transition_elapsed = 0.0f;
    float transition_duration = 0.0f;
    std::vector<PlayerAnimationFiredEvent> fired_events;
};

ResolvedPlayerAnimationGraph resolve_player_animation_graph(const std::vector<AnimationClip> &clips);

AnimationTransform animation_blend_transform(const AnimationTransform &a, const AnimationTransform &b, float alpha);
glm::mat4 animation_compose_trs(const AnimationTransform &transform);
void animation_build_global_matrices(
    const AnimationSkeleton &skeleton,
    const std::vector<AnimationTransform> &local_pose,
    std::vector<glm::mat4> &out_global);
void animation_build_skin_matrices(
    const AnimationSkeleton &skeleton,
    const std::vector<glm::mat4> &global_matrices,
    std::vector<glm::mat4> &out_skin);

PlayerAnimationSample sample_player_animation(
    const AnimationSkeleton &skeleton,
    const std::vector<AnimationClip> &clips,
    const ResolvedPlayerAnimationGraph &graph,
    PlayerAnimState state,
    float phase_radians,
    PlayerAnimState source_state,
    float source_phase_radians,
    float transition_alpha);

void append_sampled_skeleton(
    RenderMesh &dst,
    const AnimationSkeleton &skeleton,
    const PlayerAnimationSample &sample,
    const glm::vec3 &world_position,
    const glm::quat &world_rotation,
    float model_scale,
    const glm::mat4 &model_adjust,
    const glm::vec3 &color,
    float line_thickness);
