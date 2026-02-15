#pragma once

class WebPlatform {
public:
    bool init();
    void shutdown();
    bool poll_events();
};
