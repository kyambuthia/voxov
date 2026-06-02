#pragma once

#include "game/runtime_adapters.hpp"
#include "platform/android/input_android.hpp"
#include "platform/platform_input_state.hpp"

#include <android/input.h>

#include <array>

struct AndroidRuntimeSurface {
    RenderSurface surface{};
    bool focused = false;
    bool close_requested = false;
};

class AndroidRuntimeInputAdapter : public IRuntimeInputAdapter {
public:
    explicit AndroidRuntimeInputAdapter(const AndroidRuntimeSurface &surface_state);

    GameRuntimeInputFrame poll_input() override;
    int32_t handle_input(AInputEvent *event);
    void reset_frame_delta();

private:
    struct PointerState {
        int32_t id = -1;
        float start_x = 0.0f;
        float start_y = 0.0f;
        float prev_x = 0.0f;
        float prev_y = 0.0f;
    };

    enum class PointerZone {
        None,
        Move,
        Look,
        Jump,
        Sprint,
        Crouch,
        Menu,
        GodMode,
        GodF1,
        GodF2,
        GodF3,
        GodF4,
    };

    PointerZone classify(float x, float y) const;
    PointerState *find_pointer(int32_t pointer_id);
    PointerState *free_pointer();
    void sync_backend_buttons();

    const AndroidRuntimeSurface &surface_state_;
    AndroidInputBackend input_backend_{};
    std::array<PointerState, 10> pointers_{};
    glm::vec2 move_{0.0f};
    glm::vec2 look_delta_{0.0f};
    bool jump_held_ = false;
    bool sprint_held_ = false;
    bool crouch_held_ = false;
    bool menu_toggle_ = false;
    bool menu_up_ = false;
    bool menu_down_ = false;
    bool menu_select_ = false;
    bool godmode_expanded_ = false;
    bool debug_toggle_ = false;
    bool debug_xray_toggle_ = false;
    bool debug_collision_only_toggle_ = false;
    bool debug_freeze_toggle_ = false;
};

class AndroidRuntimePlatformAdapter : public IRuntimePlatformAdapter {
public:
    explicit AndroidRuntimePlatformAdapter(const AndroidRuntimeSurface &surface_state);

    RenderSurface surface() const override;
    bool should_close() const override;

private:
    const AndroidRuntimeSurface &surface_state_;
};
