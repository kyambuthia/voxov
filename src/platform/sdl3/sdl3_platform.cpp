#include "platform/sokol/sokol_platform.hpp"

#include <SDL3/SDL.h>
#include <cstdio>

#include <algorithm>
#include <cstring>

namespace {

PlatformKey map_sdl_scancode(SDL_Scancode scancode) {
    switch (scancode) {
    case SDL_SCANCODE_W:         return PlatformKey::W;
    case SDL_SCANCODE_A:         return PlatformKey::A;
    case SDL_SCANCODE_S:         return PlatformKey::S;
    case SDL_SCANCODE_D:         return PlatformKey::D;
    case SDL_SCANCODE_I:         return PlatformKey::I;
    case SDL_SCANCODE_J:         return PlatformKey::J;
    case SDL_SCANCODE_K:         return PlatformKey::K;
    case SDL_SCANCODE_L:         return PlatformKey::L;
    case SDL_SCANCODE_Q:         return PlatformKey::Q;
    case SDL_SCANCODE_R:         return PlatformKey::R;
    case SDL_SCANCODE_E:         return PlatformKey::E;
    case SDL_SCANCODE_F:         return PlatformKey::F;
    case SDL_SCANCODE_T:         return PlatformKey::T;
    case SDL_SCANCODE_C:         return PlatformKey::C;
    case SDL_SCANCODE_V:         return PlatformKey::V;
    case SDL_SCANCODE_SPACE:     return PlatformKey::Space;
    case SDL_SCANCODE_ESCAPE:    return PlatformKey::Escape;
    case SDL_SCANCODE_RETURN:    return PlatformKey::Enter;
    case SDL_SCANCODE_TAB:       return PlatformKey::Tab;
    case SDL_SCANCODE_UP:        return PlatformKey::Up;
    case SDL_SCANCODE_DOWN:      return PlatformKey::Down;
    case SDL_SCANCODE_LEFT:      return PlatformKey::Left;
    case SDL_SCANCODE_RIGHT:     return PlatformKey::Right;
    case SDL_SCANCODE_F1:        return PlatformKey::F1;
    case SDL_SCANCODE_F2:        return PlatformKey::F2;
    case SDL_SCANCODE_F3:        return PlatformKey::F3;
    case SDL_SCANCODE_F4:        return PlatformKey::F4;
    case SDL_SCANCODE_F5:        return PlatformKey::F5;
    case SDL_SCANCODE_F6:        return PlatformKey::F6;
    case SDL_SCANCODE_F11:       return PlatformKey::F11;
    case SDL_SCANCODE_F12:       return PlatformKey::F12;
    case SDL_SCANCODE_LSHIFT:    return PlatformKey::LeftShift;
    case SDL_SCANCODE_RSHIFT:    return PlatformKey::RightShift;
    case SDL_SCANCODE_LCTRL:     return PlatformKey::LeftControl;
    case SDL_SCANCODE_RCTRL:     return PlatformKey::RightControl;
    case SDL_SCANCODE_SLASH:     return PlatformKey::Slash;
    case SDL_SCANCODE_1:         return PlatformKey::Num1;
    case SDL_SCANCODE_2:         return PlatformKey::Num2;
    case SDL_SCANCODE_3:         return PlatformKey::Num3;
    case SDL_SCANCODE_4:         return PlatformKey::Num4;
    case SDL_SCANCODE_5:         return PlatformKey::Num5;
    case SDL_SCANCODE_6:         return PlatformKey::Num6;
    case SDL_SCANCODE_7:         return PlatformKey::Num7;
    case SDL_SCANCODE_8:         return PlatformKey::Num8;
    case SDL_SCANCODE_9:         return PlatformKey::Num9;
    default:                     return PlatformKey::Unknown;
    }
}

int map_sdl_mouse_button(Uint8 button) {
    switch (button) {
    case SDL_BUTTON_LEFT:   return 0;
    case SDL_BUTTON_RIGHT:  return 1;
    case SDL_BUTTON_MIDDLE: return 2;
    default:                return -1;
    }
}

} // namespace

bool DesktopPlatform::init(const PlatformCreateInfo &create_info) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    SDL_WindowFlags flags =
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (create_info.fullscreen) {
        flags |= SDL_WINDOW_FULLSCREEN;
    }

    SDL_Window *window = SDL_CreateWindow(
        create_info.title ? create_info.title : "VOXOV",
        std::max(1, create_info.width),
        std::max(1, create_info.height),
        flags);
    if (!window) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    SDL_GLContext gl_ctx = SDL_GL_CreateContext(window);
    if (!gl_ctx) {
        std::fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return false;
    }
    SDL_GL_MakeCurrent(window, gl_ctx);
    SDL_GL_SetSwapInterval(1);

    window_ = window;
    gl_context_ = gl_ctx;
    fullscreen_ = create_info.fullscreen;
    should_close_ = false;
    mouse_locked_ = false;
    input_ = {};
    return true;
}

void DesktopPlatform::shutdown() {
    if (gl_context_) {
        SDL_GL_DestroyContext(static_cast<SDL_GLContext>(gl_context_));
        gl_context_ = nullptr;
    }
    if (window_) {
        SDL_DestroyWindow(static_cast<SDL_Window *>(window_));
        window_ = nullptr;
    }
    SDL_Quit();
    input_ = {};
    should_close_ = true;
}

void DesktopPlatform::poll_events() {
    SDL_Event ev{};
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            should_close_ = true;
            break;
        case SDL_EVENT_KEY_DOWN: {
            const PlatformKey pk = map_sdl_scancode(ev.key.scancode);
            if (pk != PlatformKey::Unknown) {
                input_.keys_down.set(static_cast<size_t>(pk));
            }
            break;
        }
        case SDL_EVENT_KEY_UP: {
            const PlatformKey pk = map_sdl_scancode(ev.key.scancode);
            if (pk != PlatformKey::Unknown) {
                input_.keys_down.reset(static_cast<size_t>(pk));
            }
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            const int btn = map_sdl_mouse_button(ev.button.button);
            if (btn >= 0 && btn < 8) {
                input_.mouse_down.set(static_cast<size_t>(btn));
            }
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            const int btn = map_sdl_mouse_button(ev.button.button);
            if (btn >= 0 && btn < 8) {
                input_.mouse_down.reset(static_cast<size_t>(btn));
            }
            break;
        }
        case SDL_EVENT_MOUSE_MOTION:
            input_.mouse_pos.x = ev.motion.x;
            input_.mouse_pos.y = ev.motion.y;
            input_.mouse_delta.x += ev.motion.xrel;
            input_.mouse_delta.y += ev.motion.yrel;
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            input_.scroll_delta.x += ev.wheel.x;
            input_.scroll_delta.y += ev.wheel.y;
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            input_.keys_down.reset();
            input_.mouse_down.reset();
            input_.focused = false;
            break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            input_.focused = true;
            break;
        default:
            break;
        }
    }
}

void DesktopPlatform::swap_buffers() {
    if (window_) {
        SDL_GL_SwapWindow(static_cast<SDL_Window *>(window_));
    }
}

void DesktopPlatform::on_frame() {
    input_.mouse_delta = glm::vec2(0.0f);
    input_.scroll_delta = glm::vec2(0.0f);
}

RenderSurface DesktopPlatform::surface() const {
    int w = 1280;
    int h = 720;
    float scale = 1.0f;
    if (window_) {
        auto *win = static_cast<SDL_Window *>(window_);
        SDL_GetWindowSizeInPixels(win, &w, &h);
        scale = SDL_GetWindowDisplayScale(win);
        if (scale <= 0.0f) {
            scale = 1.0f;
        }
    }
    return { std::max(1, w), std::max(1, h), scale };
}

void DesktopPlatform::set_title(const char *title) {
    if (window_ && title) {
        SDL_SetWindowTitle(static_cast<SDL_Window *>(window_), title);
    }
}

void DesktopPlatform::set_fullscreen(bool enabled) {
    if (window_ && fullscreen_ != enabled) {
        SDL_SetWindowFullscreen(static_cast<SDL_Window *>(window_), enabled);
    }
    fullscreen_ = enabled;
}

void DesktopPlatform::toggle_fullscreen() {
    set_fullscreen(!fullscreen_);
}

bool DesktopPlatform::should_close() const {
    return should_close_;
}

void DesktopPlatform::set_mouse_lock(bool enabled) {
    if (mouse_locked_ == enabled) {
        return;
    }
    mouse_locked_ = enabled;
    if (window_) {
        SDL_SetWindowRelativeMouseMode(static_cast<SDL_Window *>(window_), enabled);
    }
}

bool DesktopPlatform::is_key_down(int key_code) const {
    if (key_code < 0 || key_code >= static_cast<int>(PlatformKey::Count)) {
        return false;
    }
    return input_.keys_down.test(static_cast<size_t>(key_code));
}

bool DesktopPlatform::is_mouse_button_down(int button) const {
    if (button < 0 || button >= 8) {
        return false;
    }
    return input_.mouse_down.test(static_cast<size_t>(button));
}

void DesktopPlatform::mouse_position(double &x, double &y) const {
    x = input_.mouse_pos.x;
    y = input_.mouse_pos.y;
}

float DesktopPlatform::mouse_delta_x() const { return input_.mouse_delta.x; }
float DesktopPlatform::mouse_delta_y() const { return input_.mouse_delta.y; }
