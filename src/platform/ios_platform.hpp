#pragma once

class IosPlatform {
public:
    bool init();
    void shutdown();
    bool poll_events();
};
