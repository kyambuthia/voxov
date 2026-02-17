#include "engine_physics/avbd_solver.hpp"

void AvbdSolver::init(const EnginePhysicsSettings &settings) {
    gravity = settings.gravity;
    initialized = true;
}

void AvbdSolver::shutdown() {
    initialized = false;
}

void AvbdSolver::step(float dt_seconds) {
    (void)dt_seconds;
    (void)gravity;
    if (!initialized) {
        return;
    }
    // AVBD solver integration point (experimental backend scaffold).
}
