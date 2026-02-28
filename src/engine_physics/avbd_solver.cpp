#include "engine_physics/avbd_solver.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include <glm/geometric.hpp>

namespace {
constexpr float kEpsilon = 1.0e-6f;

float safe_length(const glm::vec3 &v) {
    return std::max(glm::length(v), kEpsilon);
}

glm::vec3 safe_normalize(const glm::vec3 &v, const glm::vec3 &fallback = glm::vec3(0.0f, 1.0f, 0.0f)) {
    const float len = glm::length(v);
    if (len <= kEpsilon) {
        return fallback;
    }
    return v / len;
}

bool ranges_overlap(float a0, float a1, float b0, float b1) {
    return a0 <= b1 && b0 <= a1;
}
}

AvbdSpringConstraint::AvbdSpringConstraint(uint32_t a, uint32_t b, float rest_length, float stiffness)
    : v_a(a),
      v_b(b),
      rest(std::max(rest_length, 0.0f)),
      k(std::clamp(stiffness, 0.0f, 1.0f)) {}

void AvbdSpringConstraint::gather_vertices(std::vector<uint32_t> &out_vertex_ids) const {
    out_vertex_ids.push_back(v_a);
    out_vertex_ids.push_back(v_b);
}

void AvbdSpringConstraint::accumulate_vertex_term(
    const AvbdSolver &solver,
    uint32_t vertex_id,
    float rho_value,
    glm::vec3 &grad_term,
    float &diag_term) const {
    const AvbdVertexState *a = solver.try_vertex(v_a);
    const AvbdVertexState *b = solver.try_vertex(v_b);
    if (!a || !b || k <= 0.0f) {
        return;
    }

    const glm::vec3 d = a->position - b->position;
    const float dist = safe_length(d);
    const float c = dist - rest;
    const glm::vec3 dir = d / dist;
    const float rho_eff = rho_value * k;
    const float psi = lambda + rho_eff * c;

    if (vertex_id == v_a) {
        grad_term += psi * dir;
        diag_term += rho_eff;
    } else if (vertex_id == v_b) {
        grad_term -= psi * dir;
        diag_term += rho_eff;
    }
}

void AvbdSpringConstraint::update_lambda(const AvbdSolver &solver, float rho_value, float dt) {
    (void)dt;
    const AvbdVertexState *a = solver.try_vertex(v_a);
    const AvbdVertexState *b = solver.try_vertex(v_b);
    if (!a || !b || k <= 0.0f) {
        return;
    }
    const float c = safe_length(a->position - b->position) - rest;
    lambda += rho_value * k * c;
}

float AvbdSpringConstraint::max_violation(const AvbdSolver &solver) const {
    const AvbdVertexState *a = solver.try_vertex(v_a);
    const AvbdVertexState *b = solver.try_vertex(v_b);
    if (!a || !b) {
        return 0.0f;
    }
    return std::fabs(safe_length(a->position - b->position) - rest);
}

AvbdJointConstraint::AvbdJointConstraint(
    AvbdJointType joint_type,
    uint32_t a,
    uint32_t b,
    const glm::vec3 &target_offset,
    const glm::vec3 &hinge_axis)
    : type(joint_type),
      v_a(a),
      v_b(b),
      offset(target_offset),
      axis(safe_normalize(hinge_axis)) {}

void AvbdJointConstraint::gather_vertices(std::vector<uint32_t> &out_vertex_ids) const {
    out_vertex_ids.push_back(v_a);
    out_vertex_ids.push_back(v_b);
}

void AvbdJointConstraint::accumulate_vertex_term(
    const AvbdSolver &solver,
    uint32_t vertex_id,
    float rho_value,
    glm::vec3 &grad_term,
    float &diag_term) const {
    const AvbdVertexState *a = solver.try_vertex(v_a);
    const AvbdVertexState *b = solver.try_vertex(v_b);
    if (!a || !b) {
        return;
    }

    const glm::vec3 r = a->position - b->position - offset;
    const glm::vec3 psi_anchor = lambda_anchor + rho_value * r;
    const float sign = (vertex_id == v_a) ? 1.0f : ((vertex_id == v_b) ? -1.0f : 0.0f);
    if (sign == 0.0f) {
        return;
    }

    grad_term += sign * psi_anchor;
    diag_term += rho_value * 3.0f;

    if (type == AvbdJointType::Hinge) {
        const glm::vec3 c_hinge = glm::cross(axis, r);
        const glm::vec3 psi_hinge = lambda_hinge + rho_value * c_hinge;
        const glm::vec3 hinge_grad = glm::cross(psi_hinge, axis);
        grad_term += sign * hinge_grad;
        diag_term += rho_value * 2.0f;
    }
}

void AvbdJointConstraint::update_lambda(const AvbdSolver &solver, float rho_value, float dt) {
    (void)dt;
    const AvbdVertexState *a = solver.try_vertex(v_a);
    const AvbdVertexState *b = solver.try_vertex(v_b);
    if (!a || !b) {
        return;
    }

    const glm::vec3 r = a->position - b->position - offset;
    lambda_anchor += rho_value * r;
    if (type == AvbdJointType::Hinge) {
        lambda_hinge += rho_value * glm::cross(axis, r);
    }
}

float AvbdJointConstraint::max_violation(const AvbdSolver &solver) const {
    const AvbdVertexState *a = solver.try_vertex(v_a);
    const AvbdVertexState *b = solver.try_vertex(v_b);
    if (!a || !b) {
        return 0.0f;
    }

    const glm::vec3 r = a->position - b->position - offset;
    float v = glm::length(r);
    if (type == AvbdJointType::Hinge) {
        v = std::max(v, glm::length(glm::cross(axis, r)));
    }
    return v;
}

AvbdContactConstraint::AvbdContactConstraint(
    uint32_t vertex_id,
    const glm::vec3 &contact_point,
    const glm::vec3 &contact_normal,
    float friction_coeff)
    : v(vertex_id),
      p(contact_point),
      n(safe_normalize(contact_normal)),
      mu(std::max(friction_coeff, 0.0f)) {}

void AvbdContactConstraint::gather_vertices(std::vector<uint32_t> &out_vertex_ids) const {
    out_vertex_ids.push_back(v);
}

void AvbdContactConstraint::accumulate_vertex_term(
    const AvbdSolver &solver,
    uint32_t vertex_id,
    float rho_value,
    glm::vec3 &grad_term,
    float &diag_term) const {
    if (vertex_id != v) {
        return;
    }
    const AvbdVertexState *vs = solver.try_vertex(v);
    if (!vs) {
        return;
    }

    const float c_n = glm::dot(n, vs->position - p);
    const bool active = (c_n < 0.0f) || (lambda_n > 0.0f);
    if (!active) {
        return;
    }

    const float psi_n = lambda_n + rho_value * c_n;
    grad_term += psi_n * n;
    diag_term += rho_value;

    // Tangential AL term (friction) with multiplier clamped by Coulomb cone.
    const glm::vec3 tangent_vel = vs->velocity - n * glm::dot(vs->velocity, n);
    const float rho_t = rho_value * 0.25f;
    const glm::vec3 psi_t = lambda_t + rho_t * tangent_vel;
    grad_term += psi_t;
    diag_term += rho_t * 3.0f;
}

void AvbdContactConstraint::update_lambda(const AvbdSolver &solver, float rho_value, float dt) {
    const AvbdVertexState *vs = solver.try_vertex(v);
    if (!vs) {
        return;
    }
    const float c_n = glm::dot(n, vs->position - p);
    lambda_n = std::max(0.0f, lambda_n + rho_value * c_n);

    const glm::vec3 tangent_vel = vs->velocity - n * glm::dot(vs->velocity, n);
    const float rho_t = rho_value * 0.25f;
    lambda_t += rho_t * tangent_vel * std::max(dt, 0.0f);
    const float max_friction_mag = mu * lambda_n;
    const float lambda_t_mag = glm::length(lambda_t);
    if (lambda_t_mag > max_friction_mag && lambda_t_mag > kEpsilon) {
        lambda_t *= max_friction_mag / lambda_t_mag;
    }
    if (lambda_n <= 0.0f) {
        lambda_t = glm::vec3(0.0f);
    }
}

float AvbdContactConstraint::max_violation(const AvbdSolver &solver) const {
    const AvbdVertexState *vs = solver.try_vertex(v);
    if (!vs) {
        return 0.0f;
    }
    return std::max(0.0f, -glm::dot(n, vs->position - p));
}

void AvbdSolver::init(const EnginePhysicsSettings &settings) {
    gravity = glm::vec3(0.0f, settings.gravity, 0.0f);
    initialized = true;
    clear_scene();
}

void AvbdSolver::shutdown() {
    clear_scene();
    initialized = false;
}

void AvbdSolver::clear_scene() {
    vertex_state.clear();
    body_state.clear();
    persistent_constraints.clear();
    transient_constraints.clear();
    all_constraints.clear();
    constraint_graph.clear();
}

uint32_t AvbdSolver::add_body(const AvbdBodyDesc &desc) {
    const uint32_t body_id = static_cast<uint32_t>(body_state.size());
    AvbdBodyState body{};
    body.id = body_id;
    body.type = desc.type;
    body.friction = std::max(desc.friction, 0.0f);
    body.restitution = std::clamp(desc.restitution, 0.0f, 1.0f);
    body.is_static = desc.is_static || desc.mass <= 0.0f;
    body.vertices.reserve(desc.vertices.size());

    const float inv_mass = body.is_static || desc.vertices.empty()
        ? 0.0f
        : 1.0f / std::max(desc.mass / static_cast<float>(desc.vertices.size()), 1.0e-6f);
    for (const glm::vec3 &p : desc.vertices) {
        const uint32_t vertex_id = static_cast<uint32_t>(vertex_state.size());
        AvbdVertexState vertex{};
        vertex.position = p;
        vertex.predicted_position = p;
        vertex.velocity = glm::vec3(0.0f);
        vertex.inv_mass = inv_mass;
        vertex.body_id = body_id;
        vertex_state.push_back(vertex);
        body.vertices.push_back(vertex_id);
    }

    body_state.push_back(body);
    return body_id;
}

uint32_t AvbdSolver::add_rigid_box(const glm::vec3 &center, const glm::vec3 &half_extents, float mass, float friction) {
    std::vector<glm::vec3> verts;
    verts.reserve(8);
    for (int sx = -1; sx <= 1; sx += 2) {
        for (int sy = -1; sy <= 1; sy += 2) {
            for (int sz = -1; sz <= 1; sz += 2) {
                verts.push_back(center + glm::vec3(
                    static_cast<float>(sx) * half_extents.x,
                    static_cast<float>(sy) * half_extents.y,
                    static_cast<float>(sz) * half_extents.z));
            }
        }
    }

    AvbdBodyDesc body{};
    body.type = AvbdBodyType::Rigid;
    body.vertices = verts;
    body.mass = mass;
    body.friction = friction;
    const uint32_t body_id = add_body(body);
    const AvbdBodyState &added = body_state[body_id];

    // Rigid body approximation: keep all vertex pair distances hard.
    for (size_t i = 0; i < added.vertices.size(); ++i) {
        for (size_t j = i + 1; j < added.vertices.size(); ++j) {
            const uint32_t a = added.vertices[i];
            const uint32_t b = added.vertices[j];
            const float rest = glm::length(vertex_state[a].position - vertex_state[b].position);
            add_spring_constraint(a, b, rest, 1.0f);
        }
    }

    return body_id;
}

uint32_t AvbdSolver::add_soft_grid(
    const glm::vec3 &origin,
    uint32_t rows,
    uint32_t cols,
    float spacing,
    float mass_per_vertex,
    float structural_stiffness) {
    if (rows < 2 || cols < 2) {
        return std::numeric_limits<uint32_t>::max();
    }

    std::vector<glm::vec3> verts;
    verts.reserve(static_cast<size_t>(rows) * static_cast<size_t>(cols));
    for (uint32_t r = 0; r < rows; ++r) {
        for (uint32_t c = 0; c < cols; ++c) {
            verts.push_back(origin + glm::vec3(static_cast<float>(c) * spacing, 0.0f, static_cast<float>(r) * spacing));
        }
    }

    AvbdBodyDesc body{};
    body.type = AvbdBodyType::Soft;
    body.vertices = verts;
    body.mass = mass_per_vertex * static_cast<float>(verts.size());
    body.friction = 0.55f;
    const uint32_t body_id = add_body(body);
    const AvbdBodyState &added = body_state[body_id];
    auto idx = [&](uint32_t r, uint32_t c) -> uint32_t {
        return added.vertices[static_cast<size_t>(r) * cols + c];
    };

    for (uint32_t r = 0; r < rows; ++r) {
        for (uint32_t c = 0; c < cols; ++c) {
            if (c + 1 < cols) {
                const uint32_t a = idx(r, c);
                const uint32_t b = idx(r, c + 1);
                add_spring_constraint(a, b, spacing, structural_stiffness);
            }
            if (r + 1 < rows) {
                const uint32_t a = idx(r, c);
                const uint32_t b = idx(r + 1, c);
                add_spring_constraint(a, b, spacing, structural_stiffness);
            }
            if (r + 1 < rows && c + 1 < cols) {
                const uint32_t a = idx(r, c);
                const uint32_t b = idx(r + 1, c + 1);
                add_spring_constraint(a, b, spacing * 1.41421356f, structural_stiffness * 0.7f);
            }
        }
    }

    return body_id;
}

void AvbdSolver::add_spring_constraint(uint32_t a, uint32_t b, float rest_length, float stiffness) {
    persistent_constraints.push_back(std::make_unique<AvbdSpringConstraint>(a, b, rest_length, stiffness));
}

void AvbdSolver::add_joint_constraint(
    AvbdJointType type,
    uint32_t a,
    uint32_t b,
    const glm::vec3 &target_offset,
    const glm::vec3 &hinge_axis) {
    persistent_constraints.push_back(
        std::make_unique<AvbdJointConstraint>(type, a, b, target_offset, hinge_axis));
}

void AvbdSolver::set_iteration_count(uint32_t iterations) {
    max_iterations = std::max(iterations, 1u);
}

void AvbdSolver::set_augmented_penalty(float rho_value) {
    rho = std::max(rho_value, 1.0f);
}

void AvbdSolver::set_ground_height(float y) {
    ground_y = y;
}

const std::vector<AvbdVertexState> &AvbdSolver::vertices() const {
    return vertex_state;
}

const std::vector<AvbdBodyState> &AvbdSolver::bodies() const {
    return body_state;
}

const AvbdVertexState *AvbdSolver::try_vertex(uint32_t vertex_id) const {
    if (vertex_id >= vertex_state.size()) {
        return nullptr;
    }
    return &vertex_state[vertex_id];
}

void AvbdSolver::predict_vertices(float dt) {
    for (AvbdVertexState &v : vertex_state) {
        if (v.inv_mass <= 0.0f) {
            v.predicted_position = v.position;
            continue;
        }
        v.velocity += gravity * dt;
        v.predicted_position = v.position + v.velocity * dt;
        v.position = v.predicted_position;
    }
}

AvbdSolver::BodyAabb AvbdSolver::body_aabb(const AvbdBodyState &body) const {
    BodyAabb aabb{};
    aabb.min = glm::vec3(std::numeric_limits<float>::max());
    aabb.max = glm::vec3(std::numeric_limits<float>::lowest());
    for (uint32_t vi : body.vertices) {
        const glm::vec3 p = vertex_state[vi].position;
        aabb.min = glm::min(aabb.min, p);
        aabb.max = glm::max(aabb.max, p);
    }
    aabb.center = 0.5f * (aabb.min + aabb.max);
    return aabb;
}

void AvbdSolver::build_contact_constraints() {
    transient_constraints.clear();
    if (vertex_state.empty()) {
        return;
    }

    // Ground contacts.
    for (uint32_t vi = 0; vi < static_cast<uint32_t>(vertex_state.size()); ++vi) {
        const AvbdVertexState &v = vertex_state[vi];
        if (v.inv_mass <= 0.0f) {
            continue;
        }
        if (v.position.y < ground_y) {
            const AvbdBodyState &body = body_state[v.body_id];
            transient_constraints.push_back(std::make_unique<AvbdContactConstraint>(
                vi,
                glm::vec3(v.position.x, ground_y, v.position.z),
                glm::vec3(0.0f, 1.0f, 0.0f),
                body.friction));
        }
    }

    // Body-body contacts from AABB overlaps and contained vertices.
    std::vector<BodyAabb> aabbs;
    aabbs.reserve(body_state.size());
    for (const AvbdBodyState &body : body_state) {
        aabbs.push_back(body_aabb(body));
    }

    for (size_t i = 0; i < body_state.size(); ++i) {
        for (size_t j = i + 1; j < body_state.size(); ++j) {
            const BodyAabb &a = aabbs[i];
            const BodyAabb &b = aabbs[j];
            if (!ranges_overlap(a.min.x, a.max.x, b.min.x, b.max.x) ||
                !ranges_overlap(a.min.y, a.max.y, b.min.y, b.max.y) ||
                !ranges_overlap(a.min.z, a.max.z, b.min.z, b.max.z)) {
                continue;
            }

            auto emit_contacts_for = [&](const AvbdBodyState &src, const AvbdBodyState &dst, const BodyAabb &dst_aabb) {
                for (uint32_t vi : src.vertices) {
                    const AvbdVertexState &v = vertex_state[vi];
                    if (v.inv_mass <= 0.0f) {
                        continue;
                    }
                    if (v.position.x < dst_aabb.min.x || v.position.x > dst_aabb.max.x ||
                        v.position.y < dst_aabb.min.y || v.position.y > dst_aabb.max.y ||
                        v.position.z < dst_aabb.min.z || v.position.z > dst_aabb.max.z) {
                        continue;
                    }

                    const float px = std::min(v.position.x - dst_aabb.min.x, dst_aabb.max.x - v.position.x);
                    const float py = std::min(v.position.y - dst_aabb.min.y, dst_aabb.max.y - v.position.y);
                    const float pz = std::min(v.position.z - dst_aabb.min.z, dst_aabb.max.z - v.position.z);
                    glm::vec3 normal(0.0f, 1.0f, 0.0f);
                    float penetration = py;
                    if (px <= py && px <= pz) {
                        normal = glm::vec3((v.position.x > dst_aabb.center.x) ? 1.0f : -1.0f, 0.0f, 0.0f);
                        penetration = px;
                    } else if (pz <= px && pz <= py) {
                        normal = glm::vec3(0.0f, 0.0f, (v.position.z > dst_aabb.center.z) ? 1.0f : -1.0f);
                        penetration = pz;
                    } else {
                        normal = glm::vec3(0.0f, (v.position.y > dst_aabb.center.y) ? 1.0f : -1.0f, 0.0f);
                        penetration = py;
                    }

                    const glm::vec3 contact_point = v.position - normal * std::max(penetration, 0.0f);
                    transient_constraints.push_back(std::make_unique<AvbdContactConstraint>(
                        vi,
                        contact_point,
                        normal,
                        std::min(src.friction, dst.friction)));
                }
            };

            emit_contacts_for(body_state[i], body_state[j], b);
            emit_contacts_for(body_state[j], body_state[i], a);
        }
    }
}

void AvbdSolver::rebuild_constraint_graph() {
    all_constraints.clear();
    all_constraints.reserve(persistent_constraints.size() + transient_constraints.size());
    for (const auto &c : persistent_constraints) {
        all_constraints.push_back(c.get());
    }
    for (const auto &c : transient_constraints) {
        all_constraints.push_back(c.get());
    }

    constraint_graph.assign(vertex_state.size(), {});
    std::vector<uint32_t> v_ids;
    for (uint32_t ci = 0; ci < static_cast<uint32_t>(all_constraints.size()); ++ci) {
        v_ids.clear();
        all_constraints[ci]->gather_vertices(v_ids);
        for (uint32_t vi : v_ids) {
            if (vi < constraint_graph.size()) {
                constraint_graph[vi].push_back(ci);
            }
        }
    }
}

void AvbdSolver::step(float dt_seconds) {
    if (!initialized || dt_seconds <= 0.0f || vertex_state.empty()) {
        return;
    }

    const float dt = std::clamp(dt_seconds, 1.0e-4f, 1.0f / 20.0f);
    std::vector<glm::vec3> x_prev(vertex_state.size());
    for (size_t i = 0; i < vertex_state.size(); ++i) {
        x_prev[i] = vertex_state[i].position;
    }

    predict_vertices(dt);
    build_contact_constraints();
    rebuild_constraint_graph();

    // AVBD block-descent loop:
    // 1) per-vertex minimization of implicit Euler + augmented constraints
    // 2) multiplier update (lambda += rho * C)
    for (uint32_t iter = 0; iter < max_iterations; ++iter) {
        for (uint32_t vi = 0; vi < static_cast<uint32_t>(vertex_state.size()); ++vi) {
            AvbdVertexState &v = vertex_state[vi];
            if (v.inv_mass <= 0.0f) {
                continue;
            }

            const float mass_term = 1.0f / (std::max(v.inv_mass, kEpsilon) * dt * dt);
            glm::vec3 grad = mass_term * (v.position - v.predicted_position);
            float diag = mass_term;

            for (uint32_t ci : constraint_graph[vi]) {
                all_constraints[ci]->accumulate_vertex_term(*this, vi, rho, grad, diag);
            }

            if (diag > kEpsilon) {
                v.position += (-grad / diag);
            }
        }

        for (AvbdConstraint *c : all_constraints) {
            c->update_lambda(*this, rho, dt);
        }
    }

    for (size_t i = 0; i < vertex_state.size(); ++i) {
        AvbdVertexState &v = vertex_state[i];
        if (v.inv_mass <= 0.0f) {
            v.velocity = glm::vec3(0.0f);
            continue;
        }
        v.velocity = (v.position - x_prev[i]) / dt;
    }
}

void AvbdSolver::create_minimal_test_scene() {
    clear_scene();
    set_ground_height(0.0f);
    set_iteration_count(16);
    set_augmented_penalty(52.0f);

    // Stacking boxes (rigid bodies).
    const uint32_t b0 = add_rigid_box(glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.45f), 2.0f, 0.8f);
    const uint32_t b1 = add_rigid_box(glm::vec3(0.0f, 2.05f, 0.0f), glm::vec3(0.45f), 2.0f, 0.8f);
    const uint32_t b2 = add_rigid_box(glm::vec3(0.2f, 3.1f, 0.1f), glm::vec3(0.45f), 2.0f, 0.8f);
    (void)b0;
    (void)b1;
    (void)b2;

    // Soft cloth-like patch.
    const uint32_t soft = add_soft_grid(glm::vec3(-1.0f, 3.5f, -1.0f), 5, 5, 0.22f, 0.15f, 0.7f);
    if (soft != std::numeric_limits<uint32_t>::max()) {
        if (!body_state[soft].vertices.empty()) {
            const uint32_t soft_anchor_vertex = body_state[soft].vertices.front();
            // Pin one soft-body corner via fixed joint to static anchor vertex.
            AvbdBodyDesc anchor_desc{};
            anchor_desc.type = AvbdBodyType::Rigid;
            anchor_desc.vertices.push_back(glm::vec3(-1.0f, 3.5f, -1.0f));
            anchor_desc.mass = 0.0f;
            anchor_desc.is_static = true;
            const uint32_t anchor_body = add_body(anchor_desc);
            const uint32_t anchor_vertex = body_state[anchor_body].vertices.front();
            add_joint_constraint(
                AvbdJointType::Fixed,
                soft_anchor_vertex,
                anchor_vertex,
                glm::vec3(0.0f),
                glm::vec3(0.0f, 1.0f, 0.0f));
        }
    }
}
