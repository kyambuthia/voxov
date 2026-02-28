#pragma once

class ConsolePlatform {
public:
    bool init();
    void shutdown();
    bool poll_events();
};
