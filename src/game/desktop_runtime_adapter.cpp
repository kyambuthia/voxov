#include "game/desktop_runtime_adapter.hpp"
#include "platform/platform.hpp"
#include "platform/platform_input_state.hpp"

#include <glm/glm.hpp>

namespace {
InputState poll_secondary_split_input(DesktopPlatform &platform) {
    const PlatformInputSnapshot &snap = platform.input();
    InputState out{};

    auto k = [&](PlatformKey key) -> bool {
        return snap.keys_down.test(static_cast<size_t>(key));
    };

    if (k(PlatformKey::I)) { out.move.y += 1.0f; }
    if (k(PlatformKey::K)) { out.move.y -= 1.0f; }
    if (k(PlatformKey::L)) { out.move.x += 1.0f; }
    if (k(PlatformKey::J)) { out.move.x -= 1.0f; }
    if (out.move.x != 0.0f || out.move.y != 0.0f) {
        out.move = glm::normalize(out.move);
    }

    constexpr float look_speed = 5.0f;
    if (k(PlatformKey::Left))  { out.look_delta.x -= look_speed; }
    if (k(PlatformKey::Right)) { out.look_delta.x += look_speed; }
    if (k(PlatformKey::Up))    { out.look_delta.y -= look_speed; }
    if (k(PlatformKey::Down))  { out.look_delta.y += look_speed; }

    out.jump_held = k(PlatformKey::RightControl);
    out.jump_pressed = out.jump_held;
    out.sprint_held = k(PlatformKey::RightShift) || k(PlatformKey::Slash);
    out.crouch_held = k(PlatformKey::RightControl);

    return out;
}
} // namespace

DesktopRuntimeInputAdapter::DesktopRuntimeInputAdapter(
    DesktopPlatform &platform,
    bool splitscreen_enabled)
    : platform_(platform),
      input_backend_(platform),
      splitscreen_enabled_(splitscreen_enabled) {}

GameRuntimeInputFrame DesktopRuntimeInputAdapter::poll_input() {
    GameRuntimeInputFrame frame{};
    frame.primary = input_backend_.poll();
    if (splitscreen_enabled_) {
        frame.secondary = poll_secondary_split_input(platform_);
    }
    return frame;
}

void DesktopRuntimeInputAdapter::set_splitscreen_enabled(bool enabled) {
    splitscreen_enabled_ = enabled;
}

DesktopRuntimePlatformAdapter::DesktopRuntimePlatformAdapter(DesktopPlatform &platform)
    : platform_(platform) {}

bool DesktopRuntimePlatformAdapter::should_close() const {
    return platform_.should_close();
}
