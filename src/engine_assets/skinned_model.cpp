#include "engine_assets/skinned_model.hpp"

#include <cgltf.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>

#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/transform.hpp>

namespace {
bool str_contains_ci(const std::string &haystack, const char *needle) {
    if (!needle || *needle == '\0') {
        return false;
    }
    std::string h = haystack;
    std::string n = needle;
    std::transform(h.begin(), h.end(), h.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return h.find(n) != std::string::npos;
}

int node_index_from_ptr(const cgltf_data *data, const cgltf_node *node) {
    if (!data || !node) {
        return -1;
    }
    return static_cast<int>(node - data->nodes);
}

glm::mat4 read_mat4_from_accessor(const cgltf_accessor *accessor, size_t index) {
    std::array<float, 16> values{};
    cgltf_accessor_read_float(accessor, index, values.data(), values.size());
    glm::mat4 out(1.0f);
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            out[c][r] = values[static_cast<size_t>(c * 4 + r)];
        }
    }
    return out;
}
}

bool SkinnedModel::load_from_glb(const std::string &path, std::string &out_error) {
    ready = false;
    model_scale = 1.0f;
    model_ground_lift = 0.0f;
    model_axis_correction = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    model_facing_correction = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    bind_vertices.clear();
    mesh_indices.clear();
    animation_skeleton = AnimationSkeleton{};
    animation_clips.clear();
    animation_graph.states.clear();

    cgltf_options options{};
    cgltf_data *data = nullptr;
    const cgltf_result parse_result = cgltf_parse_file(&options, path.c_str(), &data);
    if (parse_result != cgltf_result_success || !data) {
        out_error = "cgltf_parse_file failed";
        return false;
    }

    const cgltf_result load_result = cgltf_load_buffers(&options, data, path.c_str());
    if (load_result != cgltf_result_success) {
        out_error = "cgltf_load_buffers failed";
        cgltf_free(data);
        return false;
    }

    animation_skeleton.nodes.resize(data->nodes_count);
    for (size_t i = 0; i < data->nodes_count; ++i) {
        AnimationSkeletonNode node{};
        const cgltf_node &src = data->nodes[i];
        node.name = src.name ? src.name : ("node_" + std::to_string(i));
        if (src.has_translation) {
            node.bind_local.translation = glm::vec3(src.translation[0], src.translation[1], src.translation[2]);
        }
        if (src.has_rotation) {
            node.bind_local.rotation = glm::quat(src.rotation[3], src.rotation[0], src.rotation[1], src.rotation[2]);
        }
        if (src.has_scale) {
            node.bind_local.scale = glm::vec3(src.scale[0], src.scale[1], src.scale[2]);
        }
        animation_skeleton.nodes[i] = node;
    }
    for (size_t i = 0; i < data->nodes_count; ++i) {
        const cgltf_node &src = data->nodes[i];
        for (size_t c = 0; c < src.children_count; ++c) {
            const int child_idx = node_index_from_ptr(data, src.children[c]);
            if (child_idx >= 0) {
                animation_skeleton.nodes[static_cast<size_t>(child_idx)].parent = static_cast<int>(i);
            }
        }
    }

    const cgltf_node *skinned_node = nullptr;
    for (size_t i = 0; i < data->nodes_count; ++i) {
        if (data->nodes[i].mesh && data->nodes[i].skin) {
            skinned_node = &data->nodes[i];
            break;
        }
    }
    if (!skinned_node) {
        out_error = "no skinned mesh node found";
        cgltf_free(data);
        return false;
    }

    const cgltf_mesh *mesh = skinned_node->mesh;
    if (!mesh || mesh->primitives_count == 0) {
        out_error = "mesh has no primitives";
        cgltf_free(data);
        return false;
    }
    const cgltf_primitive &prim = mesh->primitives[0];
    if (prim.type != cgltf_primitive_type_triangles) {
        out_error = "only triangle primitives supported";
        cgltf_free(data);
        return false;
    }

    const cgltf_accessor *pos_acc = nullptr;
    const cgltf_accessor *joints_acc = nullptr;
    const cgltf_accessor *weights_acc = nullptr;
    for (size_t i = 0; i < prim.attributes_count; ++i) {
        const cgltf_attribute &attr = prim.attributes[i];
        if (attr.type == cgltf_attribute_type_position) {
            pos_acc = attr.data;
        } else if (attr.type == cgltf_attribute_type_joints) {
            joints_acc = attr.data;
        } else if (attr.type == cgltf_attribute_type_weights) {
            weights_acc = attr.data;
        }
    }
    if (!pos_acc || !joints_acc || !weights_acc) {
        out_error = "missing POSITION/JOINTS_0/WEIGHTS_0";
        cgltf_free(data);
        return false;
    }

    const size_t vcount = pos_acc->count;
    bind_vertices.resize(vcount);
    glm::vec3 bounds_min(1.0e30f);
    glm::vec3 bounds_max(-1.0e30f);
    for (size_t i = 0; i < vcount; ++i) {
        std::array<float, 3> p{};
        std::array<float, 4> w{};
        std::array<cgltf_uint, 4> j{};
        cgltf_accessor_read_float(pos_acc, i, p.data(), 3);
        cgltf_accessor_read_float(weights_acc, i, w.data(), 4);
        cgltf_accessor_read_uint(joints_acc, i, j.data(), 4);

        float wsum = w[0] + w[1] + w[2] + w[3];
        if (wsum < 1e-5f) {
            w = {1.0f, 0.0f, 0.0f, 0.0f};
            wsum = 1.0f;
        }

        VertexBind v{};
        v.position = glm::vec3(p[0], p[1], p[2]);
        bounds_min = glm::min(bounds_min, v.position);
        bounds_max = glm::max(bounds_max, v.position);
        v.joints = glm::uvec4(j[0], j[1], j[2], j[3]);
        v.weights = glm::vec4(w[0], w[1], w[2], w[3]) / wsum;
        bind_vertices[i] = v;
    }
    const glm::vec3 extent = glm::max(bounds_max - bounds_min, glm::vec3(1.0e-4f));
    float source_height = extent.y;
    const bool force_z_up = str_contains_ci(path, "cesiumman") || str_contains_ci(path, "humanoid");
    if (force_z_up) {
        source_height = extent.z;
        model_axis_correction = glm::angleAxis(-1.57079632679f, glm::vec3(1.0f, 0.0f, 0.0f));
        // CesiumMan comes in facing +X after Z-up to Y-up conversion; rotate into engine +Z.
        model_facing_correction = glm::angleAxis(-1.57079632679f, glm::vec3(0.0f, 1.0f, 0.0f));
        model_ground_lift = -bounds_min.z + 0.02f;
    } else {
        model_facing_correction = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        model_ground_lift = -bounds_min.y + 0.02f;
    }
    constexpr float k_target_height = 1.7f;
    model_scale = k_target_height / source_height;
    model_scale = std::clamp(model_scale, 0.001f, 4.0f);

    if (prim.indices) {
        mesh_indices.resize(prim.indices->count);
        for (size_t i = 0; i < prim.indices->count; ++i) {
            mesh_indices[i] = static_cast<uint32_t>(cgltf_accessor_read_index(prim.indices, i));
        }
    } else {
        mesh_indices.resize(vcount);
        for (size_t i = 0; i < vcount; ++i) {
            mesh_indices[i] = static_cast<uint32_t>(i);
        }
    }

    const cgltf_skin *skin = skinned_node->skin;
    animation_skeleton.skin_joints.resize(skin->joints_count);
    for (size_t i = 0; i < skin->joints_count; ++i) {
        const int node_index = node_index_from_ptr(data, skin->joints[i]);
        animation_skeleton.skin_joints[i] = node_index;
        if (node_index < 0 || node_index >= static_cast<int>(animation_skeleton.nodes.size())) {
            continue;
        }
        AnimationSkeletonNode &node = animation_skeleton.nodes[static_cast<size_t>(node_index)];
        node.skin_joint = true;
        if (skin->inverse_bind_matrices && i < skin->inverse_bind_matrices->count) {
            node.inverse_bind = read_mat4_from_accessor(skin->inverse_bind_matrices, i);
        }
    }

    animation_clips.reserve(data->animations_count);
    for (size_t i = 0; i < data->animations_count; ++i) {
        const cgltf_animation &src_anim = data->animations[i];
        AnimationClip clip{};
        clip.name = src_anim.name ? src_anim.name : ("clip_" + std::to_string(i));

        for (size_t c = 0; c < src_anim.channels_count; ++c) {
            const cgltf_animation_channel &ch = src_anim.channels[c];
            const cgltf_animation_sampler *sampler = ch.sampler;
            const int node_idx = node_index_from_ptr(data, ch.target_node);
            if (!sampler || node_idx < 0 || !sampler->input || !sampler->output) {
                continue;
            }

            const size_t kcount = sampler->input->count;
            std::vector<float> times(kcount);
            for (size_t k = 0; k < kcount; ++k) {
                cgltf_accessor_read_float(sampler->input, k, &times[k], 1);
            }
            if (!times.empty()) {
                clip.duration = std::max(clip.duration, times.back());
            }

            if (ch.target_path == cgltf_animation_path_type_translation || ch.target_path == cgltf_animation_path_type_scale) {
                AnimationVec3Track vc{};
                vc.node_index = node_idx;
                vc.times = times;
                vc.values.resize(kcount);
                for (size_t k = 0; k < kcount; ++k) {
                    std::array<float, 3> out{};
                    cgltf_accessor_read_float(sampler->output, k, out.data(), 3);
                    vc.values[k] = glm::vec3(out[0], out[1], out[2]);
                }
                if (ch.target_path == cgltf_animation_path_type_translation) {
                    clip.translations.push_back(std::move(vc));
                } else {
                    clip.scales.push_back(std::move(vc));
                }
            } else if (ch.target_path == cgltf_animation_path_type_rotation) {
                AnimationQuatTrack qc{};
                qc.node_index = node_idx;
                qc.times = times;
                qc.values.resize(kcount);
                for (size_t k = 0; k < kcount; ++k) {
                    std::array<float, 4> out{};
                    cgltf_accessor_read_float(sampler->output, k, out.data(), 4);
                    qc.values[k] = glm::normalize(glm::quat(out[3], out[0], out[1], out[2]));
                }
                clip.rotations.push_back(std::move(qc));
            }
        }
        animation_clips.push_back(std::move(clip));
    }

    cgltf_free(data);
    animation_graph = resolve_player_animation_graph(animation_clips);
    ready = !bind_vertices.empty() && !mesh_indices.empty() && !animation_skeleton.skin_joints.empty();
    if (!ready) {
        out_error = "parsed model missing vertices/indices/joints";
        return false;
    }
    return true;
}

bool SkinnedModel::loaded() const {
    return ready;
}

bool SkinnedModel::has_animation() const {
    return !animation_clips.empty();
}

const AnimationSkeleton &SkinnedModel::skeleton() const {
    return animation_skeleton;
}

const std::vector<AnimationClip> &SkinnedModel::clips() const {
    return animation_clips;
}

const ResolvedPlayerAnimationGraph &SkinnedModel::graph() const {
    return animation_graph;
}

PlayerAnimationSample SkinnedModel::sample_pose(
    PlayerAnimState state,
    float anim_phase,
    PlayerAnimState source_state,
    float source_phase,
    float transition_alpha) const {
    return sample_player_animation(
        animation_skeleton,
        animation_clips,
        animation_graph,
        state,
        anim_phase,
        source_state,
        source_phase,
        transition_alpha);
}

RenderMesh SkinnedModel::build_render_mesh(
    const PlayerAnimationRuntime &runtime,
    const glm::vec3 &world_position,
    const glm::quat &world_rotation,
    const glm::vec3 &color) const {
    return build_render_mesh(
        runtime.state(),
        runtime.phase_radians(),
        runtime.previous_state(),
        runtime.previous_phase_radians(),
        runtime.transition_alpha(),
        world_position,
        world_rotation,
        color);
}

RenderMesh SkinnedModel::build_render_mesh(
    PlayerAnimState state,
    float anim_phase,
    PlayerAnimState source_state,
    float source_phase,
    float transition_alpha,
    const glm::vec3 &world_position,
    const glm::quat &world_rotation,
    const glm::vec3 &color) const {
    RenderMesh out{};
    if (!ready) {
        return out;
    }
    const PlayerAnimationSample sample = sample_pose(state, anim_phase, source_state, source_phase, transition_alpha);

    const glm::mat4 local_adjust =
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, model_ground_lift, 0.0f)) *
        glm::mat4_cast(model_facing_correction * model_axis_correction);
    const glm::mat4 world = glm::translate(glm::mat4(1.0f), world_position) * glm::mat4_cast(world_rotation) *
        glm::scale(glm::mat4(1.0f), glm::vec3(model_scale)) * local_adjust;
    out.vertices.resize(bind_vertices.size());
    out.indices = mesh_indices;

    for (size_t i = 0; i < bind_vertices.size(); ++i) {
        const VertexBind &v = bind_vertices[i];
        glm::vec4 skinned(0.0f);
        const glm::vec4 bind_pos(v.position, 1.0f);
        for (int k = 0; k < 4; ++k) {
            const uint32_t joint_idx = v.joints[static_cast<size_t>(k)];
            const float weight = v.weights[static_cast<size_t>(k)];
            if (weight <= 1e-6f || joint_idx >= sample.skin_matrices.size()) {
                continue;
            }
            skinned += (sample.skin_matrices[joint_idx] * bind_pos) * weight;
        }
        const glm::vec4 world_pos = world * glm::vec4(skinned.x, skinned.y, skinned.z, 1.0f);
        out.vertices[i].position = glm::vec3(world_pos);
        out.vertices[i].color = color;
    }

    return out;
}

void SkinnedModel::append_debug_skeleton(
    RenderMesh &dst,
    const PlayerAnimationRuntime &runtime,
    const glm::vec3 &world_position,
    const glm::quat &world_rotation,
    const glm::vec3 &color,
    float line_thickness) const {
    if (!ready) {
        return;
    }
    const PlayerAnimationSample sample = sample_pose(
        runtime.state(),
        runtime.phase_radians(),
        runtime.previous_state(),
        runtime.previous_phase_radians(),
        runtime.transition_alpha());
    const glm::mat4 local_adjust =
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, model_ground_lift, 0.0f)) *
        glm::mat4_cast(model_facing_correction * model_axis_correction);
    append_sampled_skeleton(
        dst,
        animation_skeleton,
        sample,
        world_position,
        world_rotation,
        model_scale,
        local_adjust,
        color,
        line_thickness);
}
