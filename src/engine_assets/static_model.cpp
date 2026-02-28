#include "engine_assets/static_model.hpp"

#include <cgltf.h>

#include <algorithm>
#include <array>

#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/transform.hpp>

bool StaticModel::load_from_glb(const std::string &path, std::string &out_error) {
    ready = false;
    model_scale = 1.0f;
    model_ground_lift = 0.0f;
    positions.clear();
    indices.clear();

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

    auto node_local_matrix = [](const cgltf_node &node) -> glm::mat4 {
        if (node.has_matrix) {
            glm::mat4 m(1.0f);
            for (int c = 0; c < 4; ++c) {
                for (int r = 0; r < 4; ++r) {
                    m[c][r] = static_cast<float>(node.matrix[c * 4 + r]);
                }
            }
            return m;
        }
        glm::vec3 t(0.0f);
        glm::vec3 s(1.0f);
        glm::quat q(1.0f, 0.0f, 0.0f, 0.0f);
        if (node.has_translation) {
            t = glm::vec3(
                static_cast<float>(node.translation[0]),
                static_cast<float>(node.translation[1]),
                static_cast<float>(node.translation[2]));
        }
        if (node.has_scale) {
            s = glm::vec3(
                static_cast<float>(node.scale[0]),
                static_cast<float>(node.scale[1]),
                static_cast<float>(node.scale[2]));
        }
        if (node.has_rotation) {
            q = glm::normalize(glm::quat(
                static_cast<float>(node.rotation[3]),
                static_cast<float>(node.rotation[0]),
                static_cast<float>(node.rotation[1]),
                static_cast<float>(node.rotation[2])));
        }
        return glm::translate(glm::mat4(1.0f), t) * glm::mat4_cast(q) * glm::scale(glm::mat4(1.0f), s);
    };

    auto node_world_matrix = [&](const cgltf_node *node) -> glm::mat4 {
        glm::mat4 m(1.0f);
        const cgltf_node *cur = node;
        while (cur) {
            m = node_local_matrix(*cur) * m;
            cur = cur->parent;
        }
        return m;
    };

    glm::vec3 bmin(1.0e30f);
    glm::vec3 bmax(-1.0e30f);

    auto append_primitive = [&](const cgltf_primitive &prim, const glm::mat4 &node_xform) {
        if (prim.type != cgltf_primitive_type_triangles) {
            return;
        }
        const cgltf_accessor *pos_acc = nullptr;
        for (size_t i = 0; i < prim.attributes_count; ++i) {
            if (prim.attributes[i].type == cgltf_attribute_type_position) {
                pos_acc = prim.attributes[i].data;
                break;
            }
        }
        if (!pos_acc || pos_acc->count == 0) {
            return;
        }

        const uint32_t base = static_cast<uint32_t>(positions.size());
        positions.resize(positions.size() + pos_acc->count);
        for (size_t i = 0; i < pos_acc->count; ++i) {
            float p[3] = {0.0f, 0.0f, 0.0f};
            cgltf_accessor_read_float(pos_acc, i, p, 3);
            const glm::vec4 wp = node_xform * glm::vec4(p[0], p[1], p[2], 1.0f);
            positions[base + i] = glm::vec3(wp);
            bmin = glm::min(bmin, positions[base + i]);
            bmax = glm::max(bmax, positions[base + i]);
        }

        if (prim.indices) {
            const size_t old_size = indices.size();
            indices.resize(old_size + prim.indices->count);
            for (size_t i = 0; i < prim.indices->count; ++i) {
                indices[old_size + i] = base + static_cast<uint32_t>(cgltf_accessor_read_index(prim.indices, i));
            }
        } else {
            const size_t old_size = indices.size();
            indices.resize(old_size + pos_acc->count);
            for (size_t i = 0; i < pos_acc->count; ++i) {
                indices[old_size + i] = base + static_cast<uint32_t>(i);
            }
        }
    };

    bool found_any_primitive = false;
    for (size_t n = 0; n < data->nodes_count; ++n) {
        const cgltf_node &node = data->nodes[n];
        if (!node.mesh) {
            continue;
        }
        const glm::mat4 node_xform = node_world_matrix(&node);
        for (size_t p = 0; p < node.mesh->primitives_count; ++p) {
            const size_t pos_count_before = positions.size();
            append_primitive(node.mesh->primitives[p], node_xform);
            if (positions.size() > pos_count_before) {
                found_any_primitive = true;
            }
        }
    }

    if (!found_any_primitive) {
        for (size_t m = 0; m < data->meshes_count; ++m) {
            const cgltf_mesh &mesh = data->meshes[m];
            for (size_t p = 0; p < mesh.primitives_count; ++p) {
                const size_t pos_count_before = positions.size();
                append_primitive(mesh.primitives[p], glm::mat4(1.0f));
                if (positions.size() > pos_count_before) {
                    found_any_primitive = true;
                }
            }
        }
    }
    if (!found_any_primitive) {
        out_error = "no supported primitives found";
        cgltf_free(data);
        return false;
    }

    const glm::vec3 extent = glm::max(bmax - bmin, glm::vec3(1.0e-4f));
    const float source_length = std::max(extent.x, extent.z);
    constexpr float k_target_length = 2.8f;
    model_scale = k_target_length / std::max(1.0e-4f, source_length);
    model_scale = std::clamp(model_scale, 0.01f, 6.0f);
    model_ground_lift = -bmin.y + 0.02f;

    cgltf_free(data);
    ready = !positions.empty() && !indices.empty();
    if (!ready) {
        out_error = "empty mesh";
        return false;
    }
    return true;
}

bool StaticModel::loaded() const {
    return ready;
}

RenderMesh StaticModel::build_render_mesh(
    const glm::vec3 &world_position,
    const glm::quat &world_rotation,
    const glm::vec3 &color) const {
    RenderMesh out{};
    if (!ready) {
        return out;
    }

    const glm::mat4 world = glm::translate(glm::mat4(1.0f), world_position) * glm::mat4_cast(world_rotation) *
        glm::scale(glm::mat4(1.0f), glm::vec3(model_scale));
    out.vertices.resize(positions.size());
    out.indices = indices;
    for (size_t i = 0; i < positions.size(); ++i) {
        const glm::vec4 p = world * glm::vec4(positions[i].x, positions[i].y + model_ground_lift, positions[i].z, 1.0f);
        out.vertices[i].position = glm::vec3(p.x, p.y, p.z);
        out.vertices[i].color = color;
    }
    return out;
}
