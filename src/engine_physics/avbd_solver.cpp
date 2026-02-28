#include "engine_physics/avbd_solver.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>

void AvbdSolver::initialize_demo_chain() {
    for (size_t i = 0; i < std::size(particles); ++i) {
        Particle &particle = particles[i];
        particle.y = 3.0f - static_cast<float>(i) * rest_length;
        particle.prev_y = particle.y;
        particle.inv_mass = (i == 0) ? 0.0f : 1.0f;
    }
    chain_initialized = true;
}

void AvbdSolver::solve_distance_constraints() {
    for (size_t i = 1; i < std::size(particles); ++i) {
        Particle &a = particles[i - 1];
        Particle &b = particles[i];
        const float dy = b.y - a.y;
        const float abs_dy = std::fabs(dy);
        if (abs_dy <= 1e-5f) {
            continue;
        }

        const float error = abs_dy - rest_length;
        if (std::fabs(error) <= 1e-5f) {
            continue;
        }

        const float dir = (dy >= 0.0f) ? 1.0f : -1.0f;
        const float wsum = a.inv_mass + b.inv_mass;
        if (wsum <= 1e-6f) {
            continue;
        }

        const float correction = error / wsum;
        a.y += correction * dir * a.inv_mass;
        b.y -= correction * dir * b.inv_mass;
    }
}

void AvbdSolver::init(const EnginePhysicsSettings &settings) {
    gravity = settings.gravity;
    initialize_demo_chain();
    initialized = true;
}

void AvbdSolver::shutdown() {
    chain_initialized = false;
    initialized = false;
}

void AvbdSolver::step(float dt_seconds) {
    if (!initialized || !chain_initialized || dt_seconds <= 0.0f) {
        return;
    }

    // Minimal AVBD-style positional dynamics loop used as a deterministic
    // experimental fallback: verlet integrate + iterative distance projection.
    const float dt = std::min(dt_seconds, 1.0f / 20.0f);
    const float dt2 = dt * dt;

    for (Particle &particle : particles) {
        if (particle.inv_mass <= 0.0f) {
            continue;
        }
        const float current_y = particle.y;
        const float velocity = (particle.y - particle.prev_y) * damping;
        particle.y += velocity + gravity * dt2;
        particle.prev_y = current_y;
    }

    for (uint32_t i = 0; i < solver_iterations; ++i) {
        solve_distance_constraints();
        for (Particle &particle : particles) {
            if (particle.inv_mass <= 0.0f) {
                continue;
            }
            if (particle.y < ground_height) {
                particle.y = ground_height;
            }
        }
    }
}
