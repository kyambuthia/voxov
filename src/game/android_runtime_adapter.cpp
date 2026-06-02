#include "game/android_runtime_adapter.hpp"

#include <algorithm>
#include <cmath>

AndroidRuntimeInputAdapter::AndroidRuntimeInputAdapter(
    const AndroidRuntimeSurface &surface_state)
    : surface_state_(surface_state) {}

GameRuntimeInputFrame AndroidRuntimeInputAdapter::poll_input() {
    sync_backend_buttons();
    GameRuntimeInputFrame frame{};
    frame.primary = input_backend_.poll();
    frame.touch_mode = true;
    menu_toggle_ = false;
    menu_up_ = false;
    menu_down_ = false;
    menu_select_ = false;
    look_delta_ = glm::vec2(0.0f);
    debug_toggle_ = false;
    debug_xray_toggle_ = false;
    debug_collision_only_toggle_ = false;
    debug_freeze_toggle_ = false;
    input_backend_.set_right_stick(look_delta_);
    return frame;
}

int32_t AndroidRuntimeInputAdapter::handle_input(AInputEvent *event) {
    if (event == nullptr || AInputEvent_getType(event) != AINPUT_EVENT_TYPE_MOTION) {
        return 0;
    }

    const int32_t action = AMotionEvent_getAction(event);
    const int32_t masked = action & AMOTION_EVENT_ACTION_MASK;
    const int32_t action_index =
        (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >>
        AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
    const size_t pointer_count = AMotionEvent_getPointerCount(event);

    auto update_pointer = [&](size_t index) {
        const int32_t pointer_id = AMotionEvent_getPointerId(event, index);
        PointerState *state = find_pointer(pointer_id);
        if (state == nullptr) {
            return;
        }

        const float x = AMotionEvent_getX(event, index);
        const float y = AMotionEvent_getY(event, index);
        const PointerZone zone = classify(state->start_x, state->start_y);
        if (zone == PointerZone::Move) {
            const glm::vec2 delta(x - state->start_x, y - state->start_y);
            const float radius = std::max(48.0f, surface_state_.surface.height * 0.16f);
            move_ = glm::clamp(
                glm::vec2(delta.x / radius, -delta.y / radius),
                glm::vec2(-1.0f),
                glm::vec2(1.0f));
            if (glm::length(move_) > 1.0f) {
                move_ = glm::normalize(move_);
            }
        } else if (zone == PointerZone::Look) {
            look_delta_ += glm::vec2(x - state->prev_x, y - state->prev_y);
        }
        state->prev_x = x;
        state->prev_y = y;
    };

    auto press_pointer = [&](size_t index) {
        if (index >= pointer_count) {
            return;
        }
        PointerState *state = free_pointer();
        if (state == nullptr) {
            return;
        }
        state->id = AMotionEvent_getPointerId(event, index);
        state->start_x = state->prev_x = AMotionEvent_getX(event, index);
        state->start_y = state->prev_y = AMotionEvent_getY(event, index);

        switch (classify(state->start_x, state->start_y)) {
        case PointerZone::Jump:
            jump_held_ = true;
            menu_select_ = true;
            break;
        case PointerZone::Sprint:
            sprint_held_ = true;
            menu_down_ = true;
            break;
        case PointerZone::Crouch:
            crouch_held_ = true;
            menu_up_ = true;
            break;
        case PointerZone::Menu: menu_toggle_ = true; break;
        case PointerZone::GodMode: godmode_expanded_ = !godmode_expanded_; break;
        case PointerZone::GodF1: debug_toggle_ = true; break;
        case PointerZone::GodF2: debug_xray_toggle_ = true; break;
        case PointerZone::GodF3: debug_collision_only_toggle_ = true; break;
        case PointerZone::GodF4: debug_freeze_toggle_ = true; break;
        default: break;
        }
    };

    auto release_pointer = [&](size_t index) {
        if (index >= pointer_count) {
            return;
        }
        const int32_t pointer_id = AMotionEvent_getPointerId(event, index);
        PointerState *state = find_pointer(pointer_id);
        if (state == nullptr) {
            return;
        }

        switch (classify(state->start_x, state->start_y)) {
        case PointerZone::Move: move_ = glm::vec2(0.0f); break;
        case PointerZone::Jump: jump_held_ = false; break;
        case PointerZone::Sprint: sprint_held_ = false; break;
        case PointerZone::Crouch: crouch_held_ = false; break;
        default: break;
        }
        *state = PointerState{};
    };

    if (masked == AMOTION_EVENT_ACTION_DOWN ||
        masked == AMOTION_EVENT_ACTION_POINTER_DOWN) {
        press_pointer(static_cast<size_t>(action_index));
    } else if (masked == AMOTION_EVENT_ACTION_UP ||
               masked == AMOTION_EVENT_ACTION_POINTER_UP) {
        release_pointer(static_cast<size_t>(action_index));
    } else if (masked == AMOTION_EVENT_ACTION_CANCEL) {
        pointers_ = {};
        move_ = glm::vec2(0.0f);
        look_delta_ = glm::vec2(0.0f);
        jump_held_ = false;
        sprint_held_ = false;
        crouch_held_ = false;
    } else if (masked == AMOTION_EVENT_ACTION_MOVE) {
        for (size_t i = 0; i < pointer_count; ++i) {
            update_pointer(i);
        }
    }

    sync_backend_buttons();
    return 1;
}

void AndroidRuntimeInputAdapter::reset_frame_delta() {
    look_delta_ = glm::vec2(0.0f);
    input_backend_.set_right_stick(look_delta_);
}

AndroidRuntimeInputAdapter::PointerZone
AndroidRuntimeInputAdapter::classify(float x, float y) const {
    const int width = std::max(1, surface_state_.surface.width);
    const int height = std::max(1, surface_state_.surface.height);
    const float short_edge = static_cast<float>(std::min(width, height));
    const float button = std::max(72.0f, short_edge * 0.16f);
    const float gap = std::max(14.0f, short_edge * 0.025f);

    if (x < button * 1.35f && y < button) {
        return PointerZone::Menu;
    }

    // GOD MODE toggle: top-right corner
    const float right = static_cast<float>(width) - gap;
    const float god_x0 = right - button * 1.2f;
    const float god_y0 = gap;
    const float god_x1 = right;
    const float god_y1 = gap + button;
    if (godmode_expanded_) {
        // Check GOD toggle itself first
        if (x >= god_x0 && x <= god_x1 && y >= god_y0 && y <= god_y1) {
            return PointerZone::GodMode;
        }
        // F1-F4 row just below the GOD button
        const float f_y0 = god_y1 + gap * 0.5f;
        const float f_y1 = f_y0 + button;
        const float f_total_width = button * 4.0f + gap * 3.0f;
        const float f_x_start = right - f_total_width;
        for (int i = 0; i < 4; ++i) {
            const float fx0 = f_x_start + static_cast<float>(i) * (button + gap);
            const float fx1 = fx0 + button;
            if (x >= fx0 && x <= fx1 && y >= f_y0 && y <= f_y1) {
                return static_cast<PointerZone>(
                    static_cast<int>(PointerZone::GodF1) + i);
            }
        }
    } else {
        if (x >= god_x0 && x <= god_x1 && y >= god_y0 && y <= god_y1) {
            return PointerZone::GodMode;
        }
    }

    const float bottom = static_cast<float>(height) - gap;
    const bool in_jump = x >= right - button && x <= right &&
                         y >= bottom - button && y <= bottom;
    const bool in_sprint = x >= right - button * 2.0f - gap &&
                           x <= right - button - gap &&
                           y >= bottom - button && y <= bottom;
    const bool in_crouch = x >= right - button && x <= right &&
                           y >= bottom - button * 2.0f - gap &&
                           y <= bottom - button - gap;
    if (in_jump) {
        return PointerZone::Jump;
    }
    if (in_sprint) {
        return PointerZone::Sprint;
    }
    if (in_crouch) {
        return PointerZone::Crouch;
    }

    return x < static_cast<float>(width) * 0.5f
               ? PointerZone::Move
               : PointerZone::Look;
}

AndroidRuntimeInputAdapter::PointerState *
AndroidRuntimeInputAdapter::find_pointer(int32_t pointer_id) {
    for (PointerState &pointer : pointers_) {
        if (pointer.id == pointer_id) {
            return &pointer;
        }
    }
    return nullptr;
}

AndroidRuntimeInputAdapter::PointerState *
AndroidRuntimeInputAdapter::free_pointer() {
    for (PointerState &pointer : pointers_) {
        if (pointer.id < 0) {
            return &pointer;
        }
    }
    return nullptr;
}

void AndroidRuntimeInputAdapter::sync_backend_buttons() {
    input_backend_.set_left_stick(move_);
    input_backend_.set_right_stick(look_delta_);
    input_backend_.set_jump(jump_held_);
    input_backend_.set_sprint(sprint_held_);
    input_backend_.set_crouch(crouch_held_);
    input_backend_.set_menu_toggle(menu_toggle_);
    input_backend_.set_menu_up(menu_up_);
    input_backend_.set_menu_down(menu_down_);
    input_backend_.set_menu_select(menu_select_);
    input_backend_.set_debug_toggle(debug_toggle_);
    input_backend_.set_debug_xray_toggle(debug_xray_toggle_);
    input_backend_.set_debug_collision_only_toggle(debug_collision_only_toggle_);
    input_backend_.set_debug_freeze_toggle(debug_freeze_toggle_);
}

AndroidRuntimePlatformAdapter::AndroidRuntimePlatformAdapter(
    const AndroidRuntimeSurface &surface_state)
    : surface_state_(surface_state) {}

RenderSurface AndroidRuntimePlatformAdapter::surface() const {
    return surface_state_.surface;
}

bool AndroidRuntimePlatformAdapter::should_close() const {
    return surface_state_.close_requested;
}
