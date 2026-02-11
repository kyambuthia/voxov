#pragma once

#include <cstdint>

struct EnginePhysicsSettings {
    float gravity = -9.81f;
};

class PhysicsWorld {
public:
    void init(const EnginePhysicsSettings &settings);
    void shutdown();
    void step(float dt_seconds);

private:
    void *physics_system = nullptr;
    void *temp_allocator = nullptr;
    void *job_system = nullptr;
};
