#pragma once

#include <cstdint>

// ── Platform-level event types ─────────────────────────────────────────
// These live at the Platform Independence Layer (GEA Layer 5).
// They are consumed by the input subsystem to build game-level InputState.

enum class PlatformEventType : uint8_t {
    KeyDown,
    KeyUp,
    Char,
    MouseDown,
    MouseUp,
    MouseMove,
    MouseScroll,
    TouchBegin,
    TouchMove,
    TouchEnd,
    Resized,
    Focused,
    Unfocused,
    QuitRequested,
};

/// Platform-agnostic key identifiers.
/// Concrete platform backends (sokol_app, GLFW) map their native key
/// codes into this enum.  Input backends consume only PlatformKey values.
enum class PlatformKey : uint16_t {
    Unknown = 0,

    // Movement
    W, A, S, D,
    I, J, K, L,
    Q, R, E, F, T,
    C,
    Space,
    LeftShift,  RightShift,
    LeftControl, RightControl,

    // Navigation
    Escape, Enter, Tab,
    Up, Down, Left, Right,

    // Function
    F1,  F2,  F3,  F4,  F5,
    F11, F12,

    // Misc
    Slash,

    Count  // sentinel — keep last
};

/// A single input event arriving from the platform backend.
struct PlatformEvent {
    PlatformEventType type = PlatformEventType::KeyDown;
    PlatformKey      key  = PlatformKey::Unknown;
    uint32_t         codepoint = 0;
    int              mouse_button = 0;
    float            x  = 0.0f;
    float            y  = 0.0f;
    float            dx = 0.0f;
    float            dy = 0.0f;
    float            scroll_x = 0.0f;
    float            scroll_y = 0.0f;
    int              width  = 0;
    int              height = 0;
};
