#include "game/game_runtime.hpp"

#include "engine/engine.hpp"
#include "platform/platform_services.hpp"

namespace {
EngineRuntimeOptions to_engine_options(const GameRuntimeOptions &options) {
    EngineRuntimeOptions out{};
    out.devhud = options.devhud;
    out.noclip = options.noclip;
    out.splitscreen = options.splitscreen;
    out.debug_collision = options.debug_collision;
    out.debug_xray = options.debug_xray;
    out.debug_collision_only = options.debug_collision_only;
    out.debug_freeze = options.debug_freeze;
    out.vehicle_sandbox = options.vehicle_sandbox;
    out.spherical_planet = options.spherical_planet;
    out.physics_backend = options.physics_backend;
    return out;
}
} // namespace

class GameRuntime::Impl {
public:
    bool init(const GameRuntimeInitParams &params) {
        if (params.platform_adapter == nullptr) {
            return false;
        }

        platform = params.platform;
        platform_services = params.platform_services;
        input_adapter = params.input_adapter;
        platform_adapter = params.platform_adapter;
        engine.init(platform_adapter->native_window(), to_engine_options(params.options));
        initialized = true;
        return true;
    }

    void shutdown() {
        if (!initialized) {
            return;
        }
        engine.shutdown();
        initialized = false;
    }

    void set_input_frame(const GameRuntimeInputFrame &next_input_frame) {
        input_frame = next_input_frame;
    }

    void tick(double frame_dt) {
        if (!initialized || platform_adapter == nullptr) {
            return;
        }

        // Poll native events before input so focus and cursor-lock changes are visible
        // to the input adapter within the same frame.
        platform_adapter->poll_events();
        if (input_adapter != nullptr) {
            input_frame = input_adapter->poll_input();
        }

        engine.tick(
            frame_dt,
            EngineInputFrame{
                .primary = input_frame.primary,
                .secondary = input_frame.secondary,
                .touch_mode = input_frame.touch_mode,
            });
    }

    bool should_close() const {
        return platform_adapter != nullptr && platform_adapter->should_close();
    }

    const RenderStats &stats() const {
        return engine.stats();
    }

    void connect(const char *host, uint16_t port) {
        engine.connect(host, port);
    }

    RuntimePlatform platform = RuntimePlatform::Desktop;
    const PlatformServices *platform_services = nullptr;
    Engine engine{};
    IRuntimeInputAdapter *input_adapter = nullptr;
    IRuntimePlatformAdapter *platform_adapter = nullptr;
    GameRuntimeInputFrame input_frame{};
    bool initialized = false;
};

GameRuntime::GameRuntime() : impl_(std::make_unique<Impl>()) {}

GameRuntime::~GameRuntime() = default;

GameRuntime::GameRuntime(GameRuntime &&) noexcept = default;

GameRuntime &GameRuntime::operator=(GameRuntime &&) noexcept = default;

bool GameRuntime::init(const GameRuntimeInitParams &params) {
    return impl_->init(params);
}

void GameRuntime::shutdown() {
    impl_->shutdown();
}

void GameRuntime::set_input_frame(const GameRuntimeInputFrame &input_frame) {
    impl_->set_input_frame(input_frame);
}

void GameRuntime::tick(double frame_dt) {
    impl_->tick(frame_dt);
}

bool GameRuntime::should_close() const {
    return impl_->should_close();
}

const RenderStats &GameRuntime::stats() const {
    return impl_->stats();
}

void GameRuntime::connect(const char *host, uint16_t port) {
    impl_->connect(host, port);
}
