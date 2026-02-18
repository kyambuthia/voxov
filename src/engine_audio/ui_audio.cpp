#include "engine_audio/ui_audio.hpp"

#include "engine_audio/ui_sound_data.hpp"

#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include <new>

struct UiAudio::Impl {
    ma_engine engine{};
    ma_decoder move_decoder{};
    ma_decoder click_decoder{};
    ma_sound move_sound{};
    ma_sound click_sound{};
    bool has_move = false;
    bool has_click = false;
    bool engine_ready = false;
};

bool UiAudio::init() {
    if (ready) {
        return true;
    }

    impl = new (std::nothrow) Impl();
    if (!impl) {
        return false;
    }

    if (ma_engine_init(nullptr, &impl->engine) != MA_SUCCESS) {
        delete impl;
        impl = nullptr;
        return false;
    }
    impl->engine_ready = true;

    if (ma_decoder_init_memory(kUiMoveWav, kUiMoveWavSize, nullptr, &impl->move_decoder) == MA_SUCCESS) {
        if (ma_sound_init_from_data_source(&impl->engine, &impl->move_decoder, 0, nullptr, &impl->move_sound) == MA_SUCCESS) {
            impl->has_move = true;
        } else {
            ma_decoder_uninit(&impl->move_decoder);
        }
    }

    if (ma_decoder_init_memory(kUiClickWav, kUiClickWavSize, nullptr, &impl->click_decoder) == MA_SUCCESS) {
        if (ma_sound_init_from_data_source(&impl->engine, &impl->click_decoder, 0, nullptr, &impl->click_sound) == MA_SUCCESS) {
            impl->has_click = true;
        } else {
            ma_decoder_uninit(&impl->click_decoder);
        }
    }

    ready = impl->engine_ready && (impl->has_move || impl->has_click);
    if (!ready) {
        shutdown();
    }
    return ready;
}

void UiAudio::shutdown() {
    if (!impl) {
        ready = false;
        return;
    }

    if (impl->has_move) {
        ma_sound_uninit(&impl->move_sound);
        ma_decoder_uninit(&impl->move_decoder);
        impl->has_move = false;
    }
    if (impl->has_click) {
        ma_sound_uninit(&impl->click_sound);
        ma_decoder_uninit(&impl->click_decoder);
        impl->has_click = false;
    }
    if (impl->engine_ready) {
        ma_engine_uninit(&impl->engine);
        impl->engine_ready = false;
    }

    delete impl;
    impl = nullptr;
    ready = false;
}

void UiAudio::play_move() {
    if (!ready || !impl || !impl->has_move) {
        return;
    }
    ma_sound_seek_to_pcm_frame(&impl->move_sound, 0);
    ma_sound_start(&impl->move_sound);
}

void UiAudio::play_click() {
    if (!ready || !impl || !impl->has_click) {
        return;
    }
    ma_sound_seek_to_pcm_frame(&impl->click_sound, 0);
    ma_sound_start(&impl->click_sound);
}
