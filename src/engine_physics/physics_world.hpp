#pragma once

#include "engine_physics/physics_solver.hpp"

#include <memory>

class PhysicsWorld {
public:
    void init(const EnginePhysicsSettings &settings);
    void shutdown();
    void step(float dt_seconds);

private:
    std::unique_ptr<IPhysicsSolver> solver;
};
