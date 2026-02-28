#pragma once

#include "engine_physics/physics_solver.hpp"

#include <cstdint>
#include <memory>
#include <vector>

#include <glm/vec3.hpp>

enum class AvbdBodyType : uint8_t {
    Rigid = 0,
    Soft = 1
};

enum class AvbdJointType : uint8_t {
    Fixed = 0,
    Hinge = 1,
    BallSocket = 2
};

struct AvbdVertexState {
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 predicted_position = glm::vec3(0.0f);
    glm::vec3 velocity = glm::vec3(0.0f);
    float inv_mass = 0.0f;
    uint32_t body_id = 0;
};

struct AvbdBodyState {
    uint32_t id = 0;
    AvbdBodyType type = AvbdBodyType::Rigid;
    std::vector<uint32_t> vertices;
    float friction = 0.7f;
    float restitution = 0.0f;
    bool is_static = false;
};

struct AvbdBodyDesc {
    AvbdBodyType type = AvbdBodyType::Rigid;
    std::vector<glm::vec3> vertices;
    float mass = 1.0f;
    float friction = 0.7f;
    float restitution = 0.0f;
    bool is_static = false;
};

class AvbdSolver;

class AvbdConstraint {
public:
    virtual ~AvbdConstraint() = default;

    virtual void gather_vertices(std::vector<uint32_t> &out_vertex_ids) const = 0;
    virtual void accumulate_vertex_term(
        const AvbdSolver &solver,
        uint32_t vertex_id,
        float rho,
        glm::vec3 &grad_term,
        float &diag_term) const = 0;
    virtual void update_lambda(const AvbdSolver &solver, float rho, float dt) = 0;
    virtual float max_violation(const AvbdSolver &solver) const = 0;
    virtual bool transient() const {
        return false;
    }
};

class AvbdSpringConstraint final : public AvbdConstraint {
public:
    AvbdSpringConstraint(uint32_t a, uint32_t b, float rest_length, float stiffness);

    void gather_vertices(std::vector<uint32_t> &out_vertex_ids) const override;
    void accumulate_vertex_term(
        const AvbdSolver &solver,
        uint32_t vertex_id,
        float rho,
        glm::vec3 &grad_term,
        float &diag_term) const override;
    void update_lambda(const AvbdSolver &solver, float rho, float dt) override;
    float max_violation(const AvbdSolver &solver) const override;

private:
    uint32_t v_a = 0;
    uint32_t v_b = 0;
    float rest = 0.0f;
    float k = 1.0f;
    float lambda = 0.0f;
};

class AvbdJointConstraint final : public AvbdConstraint {
public:
    AvbdJointConstraint(
        AvbdJointType joint_type,
        uint32_t a,
        uint32_t b,
        const glm::vec3 &target_offset,
        const glm::vec3 &hinge_axis);

    void gather_vertices(std::vector<uint32_t> &out_vertex_ids) const override;
    void accumulate_vertex_term(
        const AvbdSolver &solver,
        uint32_t vertex_id,
        float rho,
        glm::vec3 &grad_term,
        float &diag_term) const override;
    void update_lambda(const AvbdSolver &solver, float rho, float dt) override;
    float max_violation(const AvbdSolver &solver) const override;

private:
    AvbdJointType type = AvbdJointType::Fixed;
    uint32_t v_a = 0;
    uint32_t v_b = 0;
    glm::vec3 offset = glm::vec3(0.0f);
    glm::vec3 axis = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 lambda_anchor = glm::vec3(0.0f);
    glm::vec3 lambda_hinge = glm::vec3(0.0f);
};

class AvbdContactConstraint final : public AvbdConstraint {
public:
    AvbdContactConstraint(
        uint32_t vertex_id,
        const glm::vec3 &contact_point,
        const glm::vec3 &contact_normal,
        float friction_coeff);

    void gather_vertices(std::vector<uint32_t> &out_vertex_ids) const override;
    void accumulate_vertex_term(
        const AvbdSolver &solver,
        uint32_t vertex_id,
        float rho,
        glm::vec3 &grad_term,
        float &diag_term) const override;
    void update_lambda(const AvbdSolver &solver, float rho, float dt) override;
    float max_violation(const AvbdSolver &solver) const override;
    bool transient() const override {
        return true;
    }

private:
    uint32_t v = 0;
    glm::vec3 p = glm::vec3(0.0f);
    glm::vec3 n = glm::vec3(0.0f, 1.0f, 0.0f);
    float mu = 0.6f;
    float lambda_n = 0.0f;
    glm::vec3 lambda_t = glm::vec3(0.0f);
};

class AvbdSolver final : public IPhysicsSolver {
public:
    void init(const EnginePhysicsSettings &settings) override;
    void shutdown() override;
    void step(float dt_seconds) override;

    void clear_scene();
    void create_minimal_test_scene();

    uint32_t add_body(const AvbdBodyDesc &desc);
    uint32_t add_rigid_box(const glm::vec3 &center, const glm::vec3 &half_extents, float mass, float friction = 0.7f);
    uint32_t add_soft_grid(
        const glm::vec3 &origin,
        uint32_t rows,
        uint32_t cols,
        float spacing,
        float mass_per_vertex,
        float structural_stiffness = 0.8f);

    void add_spring_constraint(uint32_t a, uint32_t b, float rest_length, float stiffness);
    void add_joint_constraint(
        AvbdJointType type,
        uint32_t a,
        uint32_t b,
        const glm::vec3 &target_offset = glm::vec3(0.0f),
        const glm::vec3 &hinge_axis = glm::vec3(0.0f, 1.0f, 0.0f));

    void set_iteration_count(uint32_t iterations);
    void set_augmented_penalty(float rho_value);
    void set_ground_height(float y);

    const std::vector<AvbdVertexState> &vertices() const;
    const std::vector<AvbdBodyState> &bodies() const;

    const AvbdVertexState *try_vertex(uint32_t vertex_id) const;

private:
    struct BodyAabb {
        glm::vec3 min = glm::vec3(0.0f);
        glm::vec3 max = glm::vec3(0.0f);
        glm::vec3 center = glm::vec3(0.0f);
    };

    void predict_vertices(float dt);
    void build_contact_constraints();
    void rebuild_constraint_graph();
    BodyAabb body_aabb(const AvbdBodyState &body) const;

    std::vector<AvbdVertexState> vertex_state;
    std::vector<AvbdBodyState> body_state;

    std::vector<std::unique_ptr<AvbdConstraint>> persistent_constraints;
    std::vector<std::unique_ptr<AvbdConstraint>> transient_constraints;
    std::vector<AvbdConstraint *> all_constraints;
    std::vector<std::vector<uint32_t>> constraint_graph;

    glm::vec3 gravity = glm::vec3(0.0f, -9.81f, 0.0f);
    float rho = 45.0f;
    float ground_y = 0.0f;
    uint32_t max_iterations = 12;
    bool initialized = false;
};
