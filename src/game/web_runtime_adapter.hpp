#pragma once

#include "game/runtime_adapters.hpp"
#include "platform/desktop/input_desktop.hpp"
#include "platform/platform.hpp"

class WebRuntimeInputAdapter : public IRuntimeInputAdapter {
public:
    explicit WebRuntimeInputAdapter(DesktopPlatform &platform);

    GameRuntimeInputFrame poll_input() override;

private:
    DesktopPlatform &platform_;
    DesktopInputBackend input_backend_;
};

class WebRuntimePlatformAdapter : public IRuntimePlatformAdapter {
public:
    explicit WebRuntimePlatformAdapter(DesktopPlatform &platform);

    RenderSurface surface() const override;
    bool should_close() const override;

private:
    DesktopPlatform &platform_;
};
