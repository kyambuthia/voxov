#pragma once

#include "platform_events.hpp"

#include <bitset>
#include <glm/glm.hpp>

// ── RenderSurface (used by PlatformRuntime and the renderer) ─────────────
// Defined here in the platform layer (GEA Layer 5) so the render layer
// (Layer 8) can include it without creating an upward dependency.

struct RenderSurface {
    int   width     = 1;
    int   height    = 1;
    float dpi_scale = 1.0f;
};

/// Filled each frame by the platform backend.  Input backends read this
/// snapshot instead of calling platform query methods directly.
struct PlatformInputSnapshot {
    static constexpr int kKeyCount = static_cast<int>(PlatformKey::Count);

    std::bitset<kKeyCount> keys_down;
    std::bitset<8>         mouse_down;
    glm::vec2               mouse_pos{};
    glm::vec2               mouse_delta{};
    glm::vec2               scroll_delta{};
    bool                    focused = true;
};

/// Abstract platform runtime — the interface that the engine uses to
/// query window state and feed per-frame input.  Concrete backends
/// (sokol_app, GLFW) implement this.
class PlatformRuntime {
public:
    virtual ~PlatformRuntime() = default;

    /// Return the latest input snapshot (updated each frame by the backend).
    virtual const PlatformInputSnapshot &input() const = 0;

    /// Current render-surface dimensions and pixel ratio.
    virtual RenderSurface surface() const = 0;

    /// Window management.
    virtual void set_title(const char *title)     = 0;
    virtual void set_fullscreen(bool enabled)     = 0;
    virtual bool is_fullscreen() const            = 0;
    virtual void toggle_fullscreen()              = 0;

    /// Lifecycle.
    virtual bool should_close() const = 0;
};
