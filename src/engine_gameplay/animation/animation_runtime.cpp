#include "engine_gameplay/animation/animation_runtime.hpp"

#include "engine_render/debug_draw/debug_draw.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/transform.hpp>

namespace {
constexpr float k_tau = 6.28318530718f;

float normalized_phase(float phase_radians) {
    if (phase_radians <= 0.0f) {
        return 0.0f;
    }
    return std::fmod(phase_radians, k_tau) / k_tau;
}

glm::vec3 sample_vec3_track(const AnimationVec3Track &track, float time_s, const glm::vec3 &fallback) {
    if (track.times.empty() || track.values.empty()) {
        return fallback;
    }
    if (track.times.size() == 1 || time_s <= track.times.front()) {
        return track.values.front();
    }
    if (time_s >= track.times.back()) {
        return track.values.back();
    }

    const auto upper = std::upper_bound(track.times.begin(), track.times.end(), time_s);
    const size_t i1 = static_cast<size_t>(std::distance(track.times.begin(), upper));
    const size_t i0 = i1 - 1u;
    const float t0 = track.times[i0];
    const float t1 = track.times[i1];
    const float alpha = (time_s - t0) / std::max(1.0e-6f, t1 - t0);
    return glm::mix(track.values[i0], track.values[i1], alpha);
}

glm::quat sample_quat_track(const AnimationQuatTrack &track, float time_s, const glm::quat &fallback) {
    if (track.times.empty() || track.values.empty()) {
        return fallback;
    }
    if (track.times.size() == 1 || time_s <= track.times.front()) {
        return glm::normalize(track.values.front());
    }
    if (time_s >= track.times.back()) {
        return glm::normalize(track.values.back());
    }

    const auto upper = std::upper_bound(track.times.begin(), track.times.end(), time_s);
    const size_t i1 = static_cast<size_t>(std::distance(track.times.begin(), upper));
    const size_t i0 = i1 - 1u;
    const float t0 = track.times[i0];
    const float t1 = track.times[i1];
    const float alpha = (time_s - t0) / std::max(1.0e-6f, t1 - t0);
    return glm::normalize(glm::slerp(track.values[i0], track.values[i1], alpha));
}

void sample_clip_pose(
    const AnimationSkeleton &skeleton,
    const AnimationClip *clip,
    float phase_radians,
    std::vector<AnimationTransform> &out_pose) {
    out_pose.resize(skeleton.nodes.size());
    for (size_t i = 0; i < skeleton.nodes.size(); ++i) {
        out_pose[i] = skeleton.nodes[i].bind_local;
    }
    if (!clip || clip->duration <= 0.0f) {
        return;
    }

    const float clip_t = std::clamp(normalized_phase(phase_radians), 0.0f, 1.0f) * clip->duration;
    for (const AnimationVec3Track &track : clip->translations) {
        if (track.node_index < 0 || track.node_index >= static_cast<int>(out_pose.size())) {
            continue;
        }
        out_pose[static_cast<size_t>(track.node_index)].translation =
            sample_vec3_track(track, clip_t, out_pose[static_cast<size_t>(track.node_index)].translation);
    }
    for (const AnimationQuatTrack &track : clip->rotations) {
        if (track.node_index < 0 || track.node_index >= static_cast<int>(out_pose.size())) {
            continue;
        }
        out_pose[static_cast<size_t>(track.node_index)].rotation =
            sample_quat_track(track, clip_t, out_pose[static_cast<size_t>(track.node_index)].rotation);
    }
    for (const AnimationVec3Track &track : clip->scales) {
        if (track.node_index < 0 || track.node_index >= static_cast<int>(out_pose.size())) {
            continue;
        }
        out_pose[static_cast<size_t>(track.node_index)].scale =
            sample_vec3_track(track, clip_t, out_pose[static_cast<size_t>(track.node_index)].scale);
    }
}

int find_clip_index_for_alias(const std::vector<AnimationClip> &clips, const std::vector<std::string> &aliases) {
    for (const std::string &alias : aliases) {
        for (size_t i = 0; i < clips.size(); ++i) {
            if (player_animation_matches_alias(clips[i].name, alias)) {
                return static_cast<int>(i);
            }
        }
    }
    return -1;
}

const ResolvedPlayerAnimationState *find_resolved_state(
    const ResolvedPlayerAnimationGraph &graph,
    PlayerAnimState state) {
    return graph.find(state);
}

void fire_events_for_range(
    const PlayerAnimationStateDefinition &definition,
    PlayerAnimState state,
    float prev_phase,
    float next_phase,
    std::vector<PlayerAnimationFiredEvent> &out_events) {
    const float prev_norm = normalized_phase(prev_phase);
    const float next_norm = normalized_phase(next_phase);
    const bool wrapped = next_phase >= k_tau && next_norm < prev_norm;

    for (const PlayerAnimationEventDefinition &event : definition.events) {
        const float t = std::clamp(event.normalized_time, 0.0f, 1.0f);
        const bool crossed = wrapped ? (t >= prev_norm || t <= next_norm) : (t >= prev_norm && t <= next_norm);
        if (!crossed) {
            continue;
        }
        out_events.push_back(PlayerAnimationFiredEvent{state, event.type, event.payload});
    }
}
}

const ResolvedPlayerAnimationState *ResolvedPlayerAnimationGraph::find(PlayerAnimState state) const {
    const auto it = std::find_if(states.begin(), states.end(), [state](const ResolvedPlayerAnimationState &resolved) {
        return resolved.definition.state == state;
    });
    return it == states.end() ? nullptr : &(*it);
}

void PlayerAnimationRuntime::reset(PlayerAnimState state) {
    current_state = state;
    source_state = state;
    current_phase = 0.0f;
    source_phase = 0.0f;
    transition_elapsed = 0.0f;
    transition_duration = 0.0f;
    fired_events.clear();
}

void PlayerAnimationRuntime::request_state(PlayerAnimState state, float crossfade_seconds) {
    if (state == current_state) {
        return;
    }
    source_state = current_state;
    source_phase = current_phase;
    current_state = state;
    current_phase = 0.0f;
    transition_elapsed = 0.0f;
    transition_duration = std::max(0.0f, crossfade_seconds);
}

void PlayerAnimationRuntime::advance(float dt) {
    fired_events.clear();
    const PlayerAnimationStateDefinition &definition = player_animation_definition(current_state);
    const float prev_phase = current_phase;
    current_phase += definition.phase_rate * dt;
    fire_events_for_range(definition, current_state, prev_phase, current_phase, fired_events);

    advance_transition(dt);
}

void PlayerAnimationRuntime::advance_transition(float dt) {
    if (transition_duration > 0.0f) {
        transition_elapsed = std::min(transition_duration, transition_elapsed + dt);
    } else {
        transition_elapsed = transition_duration;
    }
}

void PlayerAnimationRuntime::sync_external_state(PlayerAnimState state, float phase_radians, float crossfade_seconds) {
    fired_events.clear();
    if (state != current_state) {
        source_state = current_state;
        source_phase = current_phase;
        current_state = state;
        transition_elapsed = 0.0f;
        transition_duration = std::max(0.0f, crossfade_seconds);
    }
    current_phase = phase_radians;
}

PlayerAnimState PlayerAnimationRuntime::state() const {
    return current_state;
}

PlayerAnimState PlayerAnimationRuntime::previous_state() const {
    return source_state;
}

float PlayerAnimationRuntime::phase_radians() const {
    return current_phase;
}

float PlayerAnimationRuntime::previous_phase_radians() const {
    return source_phase;
}

float PlayerAnimationRuntime::transition_alpha() const {
    if (transition_duration <= 1.0e-6f) {
        return 1.0f;
    }
    return std::clamp(transition_elapsed / transition_duration, 0.0f, 1.0f);
}

const std::vector<PlayerAnimationFiredEvent> &PlayerAnimationRuntime::events() const {
    return fired_events;
}

void PlayerAnimationRuntime::clear_events() {
    fired_events.clear();
}

ResolvedPlayerAnimationGraph resolve_player_animation_graph(const std::vector<AnimationClip> &clips) {
    ResolvedPlayerAnimationGraph out{};
    out.states.reserve(player_animation_definitions().size());

    for (const PlayerAnimationStateDefinition &definition : player_animation_definitions()) {
        ResolvedPlayerAnimationState resolved{};
        resolved.definition = definition;
        resolved.clip_index = find_clip_index_for_alias(clips, definition.clip_aliases);
        if (resolved.clip_index >= 0 && resolved.clip_index < static_cast<int>(clips.size())) {
            resolved.resolved_clip_name = clips[static_cast<size_t>(resolved.clip_index)].name;
            resolved.placeholder = false;
        }
        out.states.push_back(std::move(resolved));
    }

    for (ResolvedPlayerAnimationState &resolved : out.states) {
        if (resolved.clip_index >= 0) {
            continue;
        }
        for (PlayerAnimState fallback_state : resolved.definition.fallback_states) {
            const ResolvedPlayerAnimationState *fallback = find_resolved_state(out, fallback_state);
            if (fallback && fallback->clip_index >= 0) {
                resolved.clip_index = fallback->clip_index;
                resolved.resolved_clip_name = fallback->resolved_clip_name;
                break;
            }
        }
        if (resolved.clip_index < 0 && !clips.empty() && resolved.definition.state != PlayerAnimState::Idle) {
            resolved.clip_index = 0;
            resolved.resolved_clip_name = clips[0].name;
        }
    }

    return out;
}

AnimationTransform animation_blend_transform(const AnimationTransform &a, const AnimationTransform &b, float alpha) {
    AnimationTransform out{};
    out.translation = glm::mix(a.translation, b.translation, alpha);
    out.rotation = glm::normalize(glm::slerp(a.rotation, b.rotation, alpha));
    out.scale = glm::mix(a.scale, b.scale, alpha);
    return out;
}

glm::mat4 animation_compose_trs(const AnimationTransform &transform) {
    return glm::translate(glm::mat4(1.0f), transform.translation) *
        glm::mat4_cast(transform.rotation) *
        glm::scale(glm::mat4(1.0f), transform.scale);
}

void animation_build_global_matrices(
    const AnimationSkeleton &skeleton,
    const std::vector<AnimationTransform> &local_pose,
    std::vector<glm::mat4> &out_global) {
    out_global.resize(skeleton.nodes.size(), glm::mat4(1.0f));
    for (size_t i = 0; i < skeleton.nodes.size(); ++i) {
        const glm::mat4 local = animation_compose_trs(local_pose[i]);
        const int parent = skeleton.nodes[i].parent;
        out_global[i] = parent >= 0 ? out_global[static_cast<size_t>(parent)] * local : local;
    }
}

void animation_build_skin_matrices(
    const AnimationSkeleton &skeleton,
    const std::vector<glm::mat4> &global_matrices,
    std::vector<glm::mat4> &out_skin) {
    out_skin.resize(skeleton.skin_joints.size(), glm::mat4(1.0f));
    for (size_t i = 0; i < skeleton.skin_joints.size(); ++i) {
        const int node_index = skeleton.skin_joints[i];
        if (node_index < 0 || node_index >= static_cast<int>(skeleton.nodes.size()) ||
            node_index >= static_cast<int>(global_matrices.size())) {
            continue;
        }
        out_skin[i] = global_matrices[static_cast<size_t>(node_index)] *
            skeleton.nodes[static_cast<size_t>(node_index)].inverse_bind;
    }
}

PlayerAnimationSample sample_player_animation(
    const AnimationSkeleton &skeleton,
    const std::vector<AnimationClip> &clips,
    const ResolvedPlayerAnimationGraph &graph,
    PlayerAnimState state,
    float phase_radians,
    PlayerAnimState source_state,
    float source_phase_radians,
    float transition_alpha) {
    PlayerAnimationSample out{};
    out.state = state;
    out.local_pose.resize(skeleton.nodes.size());

    const ResolvedPlayerAnimationState *resolved = graph.find(state);
    const ResolvedPlayerAnimationState *source_resolved = graph.find(source_state);
    const AnimationClip *active_clip = nullptr;
    const AnimationClip *from_clip = nullptr;

    if (resolved && resolved->clip_index >= 0 && resolved->clip_index < static_cast<int>(clips.size())) {
        active_clip = &clips[static_cast<size_t>(resolved->clip_index)];
        out.placeholder = resolved->placeholder;
        out.clip_name = resolved->resolved_clip_name;
    }
    if (source_resolved && source_resolved->clip_index >= 0 &&
        source_resolved->clip_index < static_cast<int>(clips.size())) {
        from_clip = &clips[static_cast<size_t>(source_resolved->clip_index)];
    }

    std::vector<AnimationTransform> active_pose;
    std::vector<AnimationTransform> source_pose;
    sample_clip_pose(skeleton, active_clip, phase_radians, active_pose);
    if (transition_alpha < 0.999f && source_state != state) {
        sample_clip_pose(skeleton, from_clip, source_phase_radians, source_pose);
        out.local_pose.resize(active_pose.size());
        for (size_t i = 0; i < active_pose.size(); ++i) {
            out.local_pose[i] = animation_blend_transform(source_pose[i], active_pose[i], transition_alpha);
        }
    } else {
        out.local_pose = std::move(active_pose);
    }

    animation_build_global_matrices(skeleton, out.local_pose, out.global_matrices);
    animation_build_skin_matrices(skeleton, out.global_matrices, out.skin_matrices);
    return out;
}

void append_sampled_skeleton(
    RenderMesh &dst,
    const AnimationSkeleton &skeleton,
    const PlayerAnimationSample &sample,
    const glm::vec3 &world_position,
    const glm::quat &world_rotation,
    float model_scale,
    const glm::mat4 &model_adjust,
    const glm::vec3 &color,
    float line_thickness) {
    if (sample.global_matrices.empty()) {
        return;
    }
    const glm::mat4 world = glm::translate(glm::mat4(1.0f), world_position) *
        glm::mat4_cast(world_rotation) *
        glm::scale(glm::mat4(1.0f), glm::vec3(model_scale)) *
        model_adjust;

    auto joint_world_position = [&](size_t node_index) {
        const glm::vec4 p = world * sample.global_matrices[node_index] * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        return glm::vec3(p);
    };

    for (size_t i = 0; i < skeleton.nodes.size(); ++i) {
        const int parent = skeleton.nodes[i].parent;
        if (parent < 0) {
            continue;
        }
        append_mesh(
            dst,
            build_debug_line_mesh(
                joint_world_position(static_cast<size_t>(parent)),
                joint_world_position(i),
                line_thickness,
                color));
    }
}
