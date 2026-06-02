#include "engine_physics/physics_world.hpp"

#include "engine_physics/avbd_solver.hpp"

namespace {
std::unique_ptr<IPhysicsSolver> create_solver(PhysicsSolverBackend backend) {
    switch (backend) {
    case PhysicsSolverBackend::AvbdExperimental:
    default:
        return std::make_unique<AvbdSolver>();
    }
}
}

void PhysicsWorld::init(const EnginePhysicsSettings &settings) {
    solver = create_solver(settings.solver_backend);
    if (solver) {
        solver->init(settings);
    }
}

void PhysicsWorld::shutdown() {
    if (!solver) {
        return;
    }
    solver->shutdown();
    solver.reset();
}

void PhysicsWorld::step(float dt_seconds) {
    if (!solver) {
        return;
    }
    solver->step(dt_seconds);
}

bool PhysicsWorld::using_avbd() const {
    return dynamic_cast<const AvbdSolver *>(solver.get()) != nullptr;
}

AvbdSolver *PhysicsWorld::avbd() {
    return dynamic_cast<AvbdSolver *>(solver.get());
}

const AvbdSolver *PhysicsWorld::avbd() const {
    return dynamic_cast<const AvbdSolver *>(solver.get());
}
