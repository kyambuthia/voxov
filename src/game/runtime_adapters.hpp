#pragma once

#include "engine_input/input_state.hpp"

enum class RuntimePlatform {
    Desktop,
    Android,
    Web,
};

struct GameRuntimeInputFrame {
    InputState primary{};
    InputState secondary{};
    bool touch_mode = false;
};

class IRuntimeInputAdapter {
public:
    virtual ~IRuntimeInputAdapter() = default;
    virtual GameRuntimeInputFrame poll_input() = 0;
};

class IRuntimePlatformAdapter {
public:
    virtual ~IRuntimePlatformAdapter() = default;
    virtual void poll_events() = 0;
    virtual bool should_close() const = 0;
    virtual void *native_window() = 0;
};
