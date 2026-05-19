#include "platform/sokol/sokol_platform.hpp"

#include "sokol_app.h"

#include <cstring>
#include <cstdio>

// ---------------------------------------------------------------------------
// Sokol key → PlatformKey mapping
// ---------------------------------------------------------------------------

static PlatformKey map_sokol_key(int sokol_key) {
    switch (sokol_key) {
    case SAPP_KEYCODE_W:            return PlatformKey::W;
    case SAPP_KEYCODE_A:            return PlatformKey::A;
    case SAPP_KEYCODE_S:            return PlatformKey::S;
    case SAPP_KEYCODE_D:            return PlatformKey::D;
    case SAPP_KEYCODE_I:            return PlatformKey::I;
    case SAPP_KEYCODE_J:            return PlatformKey::J;
    case SAPP_KEYCODE_K:            return PlatformKey::K;
    case SAPP_KEYCODE_L:            return PlatformKey::L;
    case SAPP_KEYCODE_Q:            return PlatformKey::Q;
    case SAPP_KEYCODE_R:            return PlatformKey::R;
    case SAPP_KEYCODE_E:            return PlatformKey::E;
    case SAPP_KEYCODE_F:            return PlatformKey::F;
    case SAPP_KEYCODE_C:            return PlatformKey::C;
    case SAPP_KEYCODE_SPACE:        return PlatformKey::Space;
    case SAPP_KEYCODE_ESCAPE:       return PlatformKey::Escape;
    case SAPP_KEYCODE_ENTER:        return PlatformKey::Enter;
    case SAPP_KEYCODE_TAB:          return PlatformKey::Tab;
    case SAPP_KEYCODE_UP:           return PlatformKey::Up;
    case SAPP_KEYCODE_DOWN:         return PlatformKey::Down;
    case SAPP_KEYCODE_LEFT:         return PlatformKey::Left;
    case SAPP_KEYCODE_RIGHT:        return PlatformKey::Right;
    case SAPP_KEYCODE_F1:           return PlatformKey::F1;
    case SAPP_KEYCODE_F2:           return PlatformKey::F2;
    case SAPP_KEYCODE_F3:           return PlatformKey::F3;
    case SAPP_KEYCODE_F4:           return PlatformKey::F4;
    case SAPP_KEYCODE_F5:           return PlatformKey::F5;
    case SAPP_KEYCODE_F11:          return PlatformKey::F11;
    case SAPP_KEYCODE_LEFT_SHIFT:   return PlatformKey::LeftShift;
    case SAPP_KEYCODE_RIGHT_SHIFT:  return PlatformKey::RightShift;
    case SAPP_KEYCODE_LEFT_CONTROL: return PlatformKey::LeftControl;
    case SAPP_KEYCODE_RIGHT_CONTROL:return PlatformKey::RightControl;
    case SAPP_KEYCODE_SLASH:        return PlatformKey::Slash;
    default: return PlatformKey::Unknown;
    }
}

// ---------------------------------------------------------------------------
// DesktopPlatform
// ---------------------------------------------------------------------------

bool DesktopPlatform::init(const PlatformCreateInfo &create_info) {
    fullscreen_ = create_info.fullscreen;
    input_ = {};  // reset snapshot
    return true;
}

void DesktopPlatform::shutdown() {
    input_ = {};
    should_close_ = true;
}

void DesktopPlatform::process_event(const sapp_event &event) {
    switch (event.type) {
    case SAPP_EVENTTYPE_KEY_DOWN: {
        const PlatformKey pk = map_sokol_key(event.key_code);
        if (pk != PlatformKey::Unknown) {
            input_.keys_down.set(static_cast<size_t>(pk));
        }
        break;
    }
    case SAPP_EVENTTYPE_KEY_UP: {
        const PlatformKey pk = map_sokol_key(event.key_code);
        if (pk != PlatformKey::Unknown) {
            input_.keys_down.reset(static_cast<size_t>(pk));
        }
        break;
    }
    case SAPP_EVENTTYPE_MOUSE_DOWN:
        if (event.mouse_button < 8) {
            input_.mouse_down.set(static_cast<size_t>(event.mouse_button));
        }
        break;
    case SAPP_EVENTTYPE_MOUSE_UP:
        if (event.mouse_button < 8) {
            input_.mouse_down.reset(static_cast<size_t>(event.mouse_button));
        }
        break;
    case SAPP_EVENTTYPE_MOUSE_MOVE: {
        const double new_x = event.mouse_x;
        const double new_y = event.mouse_y;
        // Accumulate deltas across multiple move events per frame
        input_.mouse_delta.x += event.mouse_dx;
        input_.mouse_delta.y += event.mouse_dy;
        input_.mouse_pos.x = static_cast<float>(new_x);
        input_.mouse_pos.y = static_cast<float>(new_y);
        break;
    }
    case SAPP_EVENTTYPE_MOUSE_SCROLL:
        input_.scroll_delta.x += event.scroll_x;
        input_.scroll_delta.y += event.scroll_y;
        break;
    case SAPP_EVENTTYPE_FOCUSED:
        input_.focused = true;
        break;
    case SAPP_EVENTTYPE_UNFOCUSED:
        input_.focused = false;
        break;
    case SAPP_EVENTTYPE_RESIZED:
        // surface() reports live dimensions from sokol
        break;
    case SAPP_EVENTTYPE_QUIT_REQUESTED:
        should_close_ = true;
        break;
    default:
        break;
    }
}

void DesktopPlatform::on_frame() {
    // Reset per-frame accumulators
    input_.mouse_delta = glm::vec2(0.0f);
    input_.scroll_delta = glm::vec2(0.0f);
}

bool DesktopPlatform::should_close() const {
    return should_close_;
}

RenderSurface DesktopPlatform::surface() const {
    return { sapp_width(), sapp_height(), sapp_dpi_scale() };
}

void DesktopPlatform::set_title(const char *title) {
    sapp_set_window_title(title);
}

void DesktopPlatform::set_fullscreen(bool enabled) {
    sapp_toggle_fullscreen();
    fullscreen_ = enabled;
}

void DesktopPlatform::toggle_fullscreen() {
    sapp_toggle_fullscreen();
    fullscreen_ = !fullscreen_;
}

void DesktopPlatform::set_mouse_lock(bool enabled) {
    if (mouse_locked_ == enabled) {
        return;
    }
    mouse_locked_ = enabled;
    sapp_lock_mouse(enabled);
}

// ── Legacy query helpers (bridge for incremental migration) ───────────

bool DesktopPlatform::is_key_down(int key_code) const {
    // Keep supporting raw key-codes for existing DesktopInputBackend.
    // Map common GLFW codes to PlatformKey for the snapshot.
    static constexpr int kMaxCode = 512;
    if (key_code < 0 || key_code >= kMaxCode) return false;

    // Quick lookup: map GLFW keycode → PlatformKey
    auto code_to_key = [](int code) -> PlatformKey {
        switch (code) {
        case 87:  return PlatformKey::W;
        case 65:  return PlatformKey::A;
        case 83:  return PlatformKey::S;
        case 68:  return PlatformKey::D;
        case 73:  return PlatformKey::I;
        case 74:  return PlatformKey::J;
        case 75:  return PlatformKey::K;
        case 76:  return PlatformKey::L;
        case 81:  return PlatformKey::Q;
        case 82:  return PlatformKey::R;
        case 69:  return PlatformKey::E;
        case 70:  return PlatformKey::F;
        case 67:  return PlatformKey::C;
        case 32:  return PlatformKey::Space;
        case 256: return PlatformKey::Escape;
        case 257: return PlatformKey::Enter;
        case 265: return PlatformKey::Up;
        case 264: return PlatformKey::Down;
        case 263: return PlatformKey::Left;
        case 262: return PlatformKey::Right;
        case 290: return PlatformKey::F1;
        case 291: return PlatformKey::F2;
        case 292: return PlatformKey::F3;
        case 293: return PlatformKey::F4;
        case 294: return PlatformKey::F5;
        case 303: return PlatformKey::F11;
        case 340: return PlatformKey::LeftShift;
        case 341: return PlatformKey::LeftControl;
        case 47:  return PlatformKey::Slash;
        default:  return PlatformKey::Unknown;
        }
    };

    const PlatformKey pk = code_to_key(key_code);
    if (pk == PlatformKey::Unknown) return false;
    return input_.keys_down.test(static_cast<size_t>(pk));
}

bool DesktopPlatform::is_mouse_button_down(int button) const {
    if (button < 0 || button >= 8) return false;
    return input_.mouse_down.test(static_cast<size_t>(button));
}

void DesktopPlatform::mouse_position(double &x, double &y) const {
    x = static_cast<double>(input_.mouse_pos.x);
    y = static_cast<double>(input_.mouse_pos.y);
}

float DesktopPlatform::mouse_delta_x() const {
    return input_.mouse_delta.x;
}

float DesktopPlatform::mouse_delta_y() const {
    return input_.mouse_delta.y;
}
