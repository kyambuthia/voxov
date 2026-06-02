#pragma once

enum class PhysicsSolverBackend {
    AvbdExperimental
};

struct EnginePhysicsSettings {
    float gravity = -9.81f;
    PhysicsSolverBackend solver_backend = PhysicsSolverBackend::AvbdExperimental;
};

class IPhysicsSolver {
public:
    virtual ~IPhysicsSolver() = default;

    virtual void init(const EnginePhysicsSettings &settings) = 0;
    virtual void shutdown() = 0;
    virtual void step(float dt_seconds) = 0;
};
