#include "engine_assets/skinned_model.hpp"

#include <cgltf.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>

#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/transform.hpp>
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
    bind_vertices.clear();
    mesh_indices.clear();
    nodes.clear();
    joints.clear();
    clips.clear();

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

    nodes.resize(data->nodes_count);
    for (size_t i = 0; i < data->nodes_count; ++i) {
        NodeTransform n{};
        const cgltf_node &src = data->nodes[i];
        if (src.has_translation) {
            n.translation = glm::vec3(src.translation[0], src.translation[1], src.translation[2]);
        }
        if (src.has_rotation) {
            n.rotation = glm::quat(src.rotation[3], src.rotation[0], src.rotation[1], src.rotation[2]);
        }
        if (src.has_scale) {
            n.scale = glm::vec3(src.scale[0], src.scale[1], src.scale[2]);
        }
        nodes[i] = n;
    }
    for (size_t i = 0; i < data->nodes_count; ++i) {
        const cgltf_node &src = data->nodes[i];
        for (size_t c = 0; c < src.children_count; ++c) {
            const int child_idx = node_index_from_ptr(data, src.children[c]);
            if (child_idx >= 0) {
                nodes[i].children.push_back(child_idx);
                nodes[static_cast<size_t>(child_idx)].parent = static_cast<int>(i);
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
        // CesiumMan faces opposite the engine's expected forward after axis conversion.
        model_facing_correction = glm::angleAxis(3.14159265359f, glm::vec3(0.0f, 1.0f, 0.0f));
        model_ground_lift = -bounds_min.z + 0.08f;
    } else {
        model_facing_correction = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        model_ground_lift = -bounds_min.y + 0.08f;
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
    joints.resize(skin->joints_count);
    for (size_t i = 0; i < skin->joints_count; ++i) {
        Joint j{};
        j.node_index = node_index_from_ptr(data, skin->joints[i]);
        if (skin->inverse_bind_matrices && i < skin->inverse_bind_matrices->count) {
            j.inverse_bind = read_mat4_from_accessor(skin->inverse_bind_matrices, i);
        }
        joints[i] = j;
    }

    clips.reserve(data->animations_count);
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
                Vec3Channel vc{};
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
                QuatChannel qc{};
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
        clips.push_back(std::move(clip));
    }

    cgltf_free(data);
    ready = !bind_vertices.empty() && !mesh_indices.empty() && !joints.empty();
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
    return !clips.empty();
}

int SkinnedModel::select_clip(PlayerAnimState state) const {
    if (clips.empty()) {
        return -1;
    }
    auto find_by = [&](const char *needle) -> int {
        for (size_t i = 0; i < clips.size(); ++i) {
            if (str_contains_ci(clips[i].name, needle)) {
                return static_cast<int>(i);
            }
        }
        return -1;
    };

    if (state == PlayerAnimState::Run) {
        int i = find_by("run");
        if (i >= 0) {
            return i;
        }
    }
    if (state == PlayerAnimState::Walk || state == PlayerAnimState::Crawl) {
        int i = find_by("walk");
        if (i >= 0) {
            return i;
        }
    }
    if (state == PlayerAnimState::Idle) {
        int i = find_by("idle");
        if (i >= 0) {
            return i;
        }
        i = find_by("stand");
        if (i >= 0) {
            return i;
        }
        i = find_by("breath");
        if (i >= 0) {
            return i;
        }
        i = find_by("survey");
        if (i >= 0) {
            return i;
        }
        // Prefer a static bind pose over an arbitrary motion clip when truly idle.
        return -1;
    }
    int fallback_walk = find_by("walk");
    if (fallback_walk >= 0) {
        return fallback_walk;
    }
    return 0;
}

glm::vec3 SkinnedModel::sample_vec3_channel(const Vec3Channel &channel, float time_s) {
    if (channel.times.empty() || channel.values.empty()) {
        return glm::vec3(0.0f);
    }
    if (channel.times.size() == 1) {
        return channel.values[0];
    }
    if (time_s <= channel.times.front()) {
        return channel.values.front();
    }
    if (time_s >= channel.times.back()) {
        return channel.values.back();
    }

    auto upper = std::upper_bound(channel.times.begin(), channel.times.end(), time_s);
    const size_t i1 = static_cast<size_t>(std::distance(channel.times.begin(), upper));
    const size_t i0 = i1 - 1;
    const float t0 = channel.times[i0];
    const float t1 = channel.times[i1];
    const float alpha = (time_s - t0) / std::max(1e-6f, t1 - t0);
    return glm::mix(channel.values[i0], channel.values[i1], alpha);
}

glm::quat SkinnedModel::sample_quat_channel(const QuatChannel &channel, float time_s) {
    if (channel.times.empty() || channel.values.empty()) {
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    }
    if (channel.times.size() == 1) {
        return channel.values[0];
    }
    if (time_s <= channel.times.front()) {
        return channel.values.front();
    }
    if (time_s >= channel.times.back()) {
        return channel.values.back();
    }

    auto upper = std::upper_bound(channel.times.begin(), channel.times.end(), time_s);
    const size_t i1 = static_cast<size_t>(std::distance(channel.times.begin(), upper));
    const size_t i0 = i1 - 1;
    const float t0 = channel.times[i0];
    const float t1 = channel.times[i1];
    const float alpha = (time_s - t0) / std::max(1e-6f, t1 - t0);
    return glm::normalize(glm::slerp(channel.values[i0], channel.values[i1], alpha));
}

glm::mat4 SkinnedModel::compose_trs(const glm::vec3 &t, const glm::quat &r, const glm::vec3 &s) {
    return glm::translate(glm::mat4(1.0f), t) * glm::mat4_cast(r) * glm::scale(glm::mat4(1.0f), s);
}

void SkinnedModel::compute_global_matrices(
    const std::vector<glm::vec3> &local_t,
    const std::vector<glm::quat> &local_r,
    const std::vector<glm::vec3> &local_s,
    std::vector<glm::mat4> &out_global) const {
    out_global.resize(nodes.size(), glm::mat4(1.0f));
    for (size_t i = 0; i < nodes.size(); ++i) {
        const glm::mat4 local = compose_trs(local_t[i], local_r[i], local_s[i]);
        const int parent = nodes[i].parent;
        out_global[i] = (parent >= 0) ? out_global[static_cast<size_t>(parent)] * local : local;
    }
}

RenderMesh SkinnedModel::build_render_mesh(
    PlayerAnimState state,
    float anim_phase,
    float anim_blend,
    const glm::vec3 &world_position,
    const glm::quat &world_rotation,
    const glm::vec3 &color) const {
    (void)anim_blend;
    RenderMesh out{};
    if (!ready) {
        return out;
    }

    std::vector<glm::vec3> local_t(nodes.size());
    std::vector<glm::quat> local_r(nodes.size());
    std::vector<glm::vec3> local_s(nodes.size());
    for (size_t i = 0; i < nodes.size(); ++i) {
        local_t[i] = nodes[i].translation;
        local_r[i] = nodes[i].rotation;
        local_s[i] = nodes[i].scale;
    }

    const int clip_idx = select_clip(state);
    if (clip_idx >= 0 && clip_idx < static_cast<int>(clips.size()) && clips[static_cast<size_t>(clip_idx)].duration > 0.0f) {
        const AnimationClip &clip = clips[static_cast<size_t>(clip_idx)];
        const float loop_t = std::fmod((anim_phase / 6.28318530718f) * clip.duration, clip.duration);
        for (const Vec3Channel &ch : clip.translations) {
            local_t[static_cast<size_t>(ch.node_index)] = sample_vec3_channel(ch, loop_t);
        }
        for (const QuatChannel &ch : clip.rotations) {
            local_r[static_cast<size_t>(ch.node_index)] = sample_quat_channel(ch, loop_t);
        }
        for (const Vec3Channel &ch : clip.scales) {
            local_s[static_cast<size_t>(ch.node_index)] = sample_vec3_channel(ch, loop_t);
        }
    }

    std::vector<glm::mat4> global_nodes;
    compute_global_matrices(local_t, local_r, local_s, global_nodes);

    std::vector<glm::mat4> joint_mats(joints.size(), glm::mat4(1.0f));
    for (size_t i = 0; i < joints.size(); ++i) {
        const int node_idx = joints[i].node_index;
        if (node_idx >= 0 && node_idx < static_cast<int>(global_nodes.size())) {
            joint_mats[i] = global_nodes[static_cast<size_t>(node_idx)] * joints[i].inverse_bind;
        }
    }

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
            if (weight <= 1e-6f || joint_idx >= joint_mats.size()) {
                continue;
            }
            skinned += (joint_mats[joint_idx] * bind_pos) * weight;
        }
        const glm::vec4 world_pos = world * glm::vec4(skinned.x, skinned.y, skinned.z, 1.0f);
        out.vertices[i].position = glm::vec3(world_pos);
        out.vertices[i].color = color;
    }

    return out;
}
