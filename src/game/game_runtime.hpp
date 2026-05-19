#pragma once

#include "engine_physics/physics_solver.hpp"
#include "engine_render/render_backend_type.hpp"
#include "engine_render/render_types.hpp"
#include "game/runtime_adapters.hpp"

#include <memory>

struct PlatformServices;

struct GameRuntimeOptions {
    PhysicsSolverBackend physics_backend = PhysicsSolverBackend::Jolt;
    RenderBackendType render_backend = RenderBackendType::Sokol;
};

struct GameRuntimeInitParams {
    RuntimePlatform platform = RuntimePlatform::Desktop;
    GameRuntimeOptions options{};
    IRuntimeInputAdapter *input_adapter = nullptr;
    IRuntimePlatformAdapter *platform_adapter = nullptr;
    const PlatformServices *platform_services = nullptr;
};

class GameRuntime {
public:
    GameRuntime();
    ~GameRuntime();

    GameRuntime(const GameRuntime &) = delete;
    GameRuntime &operator=(const GameRuntime &) = delete;
    GameRuntime(GameRuntime &&) noexcept;
    GameRuntime &operator=(GameRuntime &&) noexcept;

    bool init(const GameRuntimeInitParams &params);
    void shutdown();

    void set_input_frame(const GameRuntimeInputFrame &input_frame);
    void tick(double frame_dt);

    bool should_close() const;
    const RenderStats &stats() const;

    void connect(const char *host, uint16_t port);

private:
    // Keep Engine behind a runtime-facing shell so platform extraction can proceed
    // before the simulation state is moved out of Engine.
    class Impl;
    std::unique_ptr<Impl> impl_;
};
