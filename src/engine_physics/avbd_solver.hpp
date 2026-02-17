#pragma once

#include "engine_physics/physics_solver.hpp"

class AvbdSolver final : public IPhysicsSolver {
public:
    void init(const EnginePhysicsSettings &settings) override;
    void shutdown() override;
    void step(float dt_seconds) override;

private:
    float gravity = -9.81f;
    bool initialized = false;
};
