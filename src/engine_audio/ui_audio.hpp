#pragma once

#include <cstdint>

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
    void play_block_break();
    void play_block_place();
    void play_footstep(uint8_t material_id);

private:
    bool ready = false;
    struct Impl;
    Impl *impl = nullptr;
};
