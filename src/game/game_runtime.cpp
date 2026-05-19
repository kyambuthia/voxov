#include "game/game_runtime.hpp"

#include "engine_audio/ui_audio.hpp"
#include "engine/engine.hpp"
#include "engine_runtime/runtime_session_controller.hpp"
#include "engine_ui/gui_menu.hpp"
#include "game/runtime_session_flow.hpp"
#include "platform/platform_services.hpp"

namespace {
EngineRuntimeOptions to_engine_options(const GameRuntimeOptions &options) {
    EngineRuntimeOptions out{};
    out.physics_backend = options.physics_backend;
    out.render_backend = options.render_backend;
    return out;
}
} // namespace

class GameRuntime::Impl {
public:
    bool init(const GameRuntimeInitParams &params) {
        if (params.platform_adapter == nullptr) {
            return false;
        }

        runtime_stats = RenderStats{};
        fps_accumulator = 0.0;
        fps_frames = 0;
        platform = params.platform;
        platform_services = params.platform_services;
        input_adapter = params.input_adapter;
        platform_adapter = params.platform_adapter;
        session_controller.set_gameplay_started(false);
        EngineRuntimeOptions engine_options = to_engine_options(params.options);
        engine_options.platform_services = params.platform_services;
        engine.init(engine_options);
        ui_audio.init();
        gui_menu.set_character(engine.preferred_character());
        sync_session_state();
        initialized = true;
        return true;
    }

    void shutdown() {
        if (!initialized) {
            return;
        }
        ui_audio.shutdown();
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

        // Input is event-fed by sokol_app callbacks or polled by GLFW before tick.
        // The input adapter produces the latest frame state.
        if (input_adapter != nullptr) {
            input_frame = input_adapter->poll_input();
        }

        const RuntimeSessionMenuCallbacks callbacks{
            .leave_session = [this]() { session_flow.leave_session(engine); },
            .host_local = [this]() { session_flow.host_local_session(engine); },
            .host_lan = [this]() { session_flow.host_lan_session(engine); },
            .join_nearby = [this]() { session_flow.join_nearby_session(engine); },
        };
        const RuntimeMenuResult menu_result =
            session_controller.handle_menu_input(
                input_frame.primary,
                gui_menu,
                callbacks);
        if (menu_result.ui_move_sfx) {
            ui_audio.play_move();
        }
        if (menu_result.ui_select_sfx) {
            ui_audio.play_click();
        }
        if (menu_result.reset_camera_requested) {
            engine.reset_camera();
        }

        session_flow.update(engine, frame_dt);
        sync_session_state();

        const RenderSurface surface = platform_adapter->surface();
        engine.tick(
            frame_dt,
            EngineInputFrame{
                .primary = input_frame.primary,
                .secondary = input_frame.secondary,
                .touch_mode = input_frame.touch_mode,
            },
            surface);
        const double previous_fps = runtime_stats.fps;
        runtime_stats = engine.stats();
        runtime_stats.fps = previous_fps;
        fps_accumulator += frame_dt;
        fps_frames++;
        if (fps_accumulator >= 0.3) {
            runtime_stats.fps = static_cast<double>(fps_frames) / fps_accumulator;
            fps_accumulator = 0.0;
            fps_frames = 0;
        }
    }

    bool should_close() const {
        return platform_adapter != nullptr && platform_adapter->should_close();
    }

    const RenderStats &stats() const {
        return runtime_stats;
    }

    void connect(const char *host, uint16_t port) {
        session_flow.connect(engine, host, port);
    }

private:
    void sync_session_state() {
        const RuntimeSessionSnapshot snapshot =
            session_flow.build_snapshot(engine.session_snapshot());
        engine.set_session_state(EngineSessionState{
            .gameplay_started = session_controller.gameplay_started(),
            .menu_open = gui_menu.open(),
            .selected_character = gui_menu.character(),
            .menu_view = gui_menu.build_view(
                false,
                false,
                session_controller.build_session_context(snapshot)),
        });
    }

    RuntimePlatform platform = RuntimePlatform::Desktop;
    const PlatformServices *platform_services = nullptr;
    Engine engine{};
    RuntimeSessionFlow session_flow{};
    RuntimeSessionController session_controller{};
    GuiMenu gui_menu{};
    UiAudio ui_audio{};
    IRuntimeInputAdapter *input_adapter = nullptr;
    IRuntimePlatformAdapter *platform_adapter = nullptr;
    GameRuntimeInputFrame input_frame{};
    RenderStats runtime_stats{};
    double fps_accumulator = 0.0;
    uint32_t fps_frames = 0;
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
