#include "game/web_runtime_adapter.hpp"

WebRuntimeInputAdapter::WebRuntimeInputAdapter(DesktopPlatform &platform)
    : platform_(platform),
      input_backend_(platform) {}

GameRuntimeInputFrame WebRuntimeInputAdapter::poll_input() {
    GameRuntimeInputFrame frame{};
    frame.primary = input_backend_.poll();
    return frame;
}

WebRuntimePlatformAdapter::WebRuntimePlatformAdapter(DesktopPlatform &platform)
    : platform_(platform) {}

RenderSurface WebRuntimePlatformAdapter::surface() const {
    return platform_.surface();
}

bool WebRuntimePlatformAdapter::should_close() const {
    return platform_.should_close();
}
