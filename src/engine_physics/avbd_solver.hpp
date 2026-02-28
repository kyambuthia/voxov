#pragma once

#include "engine_physics/physics_solver.hpp"

#include <cstdint>

class AvbdSolver final : public IPhysicsSolver {
public:
    void init(const EnginePhysicsSettings &settings) override;
    void shutdown() override;
    void step(float dt_seconds) override;

private:
    struct Particle {
        float y = 0.0f;
        float prev_y = 0.0f;
        float inv_mass = 1.0f;
    };

    void initialize_demo_chain();
    void solve_distance_constraints();

    float gravity = -9.81f;
    float damping = 0.992f;
    float ground_height = -1.0f;
    float rest_length = 0.45f;
    uint32_t solver_iterations = 6;
    Particle particles[8]{};
    bool chain_initialized = false;
    bool initialized = false;
};
