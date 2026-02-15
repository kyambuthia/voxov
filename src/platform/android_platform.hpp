#pragma once

class AndroidPlatform {
public:
    bool init();
    void shutdown();
    bool poll_events();
};
