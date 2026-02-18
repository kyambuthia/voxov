#pragma once

class UiAudio {
public:
    UiAudio() = default;
    ~UiAudio() = default;
    UiAudio(const UiAudio &) = delete;
    UiAudio &operator=(const UiAudio &) = delete;
    UiAudio(UiAudio &&) = delete;
    UiAudio &operator=(UiAudio &&) = delete;

    bool init();
    void shutdown();
    void play_move();
    void play_click();

private:
    bool ready = false;
    struct Impl;
    Impl *impl = nullptr;
};
