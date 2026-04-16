#pragma once

#include "game/runtime_adapters.hpp"
#include "platform/desktop/input_desktop.hpp"
#include "platform/platform.hpp"

class DesktopRuntimeInputAdapter : public IRuntimeInputAdapter {
public:
    DesktopRuntimeInputAdapter(DesktopPlatform &platform, bool splitscreen_enabled);

    GameRuntimeInputFrame poll_input() override;
    void set_splitscreen_enabled(bool enabled);

private:
    DesktopPlatform &platform_;
    DesktopInputBackend input_backend_;
    bool splitscreen_enabled_ = false;
};

class DesktopRuntimePlatformAdapter : public IRuntimePlatformAdapter {
public:
    explicit DesktopRuntimePlatformAdapter(DesktopPlatform &platform);

    void poll_events() override;
    bool should_close() const override;
    void *native_window() override;

private:
    DesktopPlatform &platform_;
};
