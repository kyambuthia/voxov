#pragma once

#include "engine_physics/physics_solver.hpp"

class JoltSolver final : public IPhysicsSolver {
public:
    void init(const EnginePhysicsSettings &settings) override;
    void shutdown() override;
    void step(float dt_seconds) override;

private:
    void *physics_system = nullptr;
    void *temp_allocator = nullptr;
    void *job_system = nullptr;
};
