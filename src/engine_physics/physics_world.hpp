#pragma once

#include "engine_physics/physics_solver.hpp"

#include <memory>

class AvbdSolver;

class PhysicsWorld {
public:
    void init(const EnginePhysicsSettings &settings);
    void shutdown();
    void step(float dt_seconds);

    bool using_avbd() const;
    AvbdSolver *avbd();
    const AvbdSolver *avbd() const;

private:
    std::unique_ptr<IPhysicsSolver> solver;
};
