#include "engine_audio/ui_audio.hpp"

#include "sokol_audio.h"
#include "sokol_log.h"

#include <algorithm>
#include <cmath>
#include <new>

namespace {

constexpr float kPi = 3.14159265358979323846f;

struct UiVoice {
    float phase = 0.0f;
    float frequency = 440.0f;
    int frames_remaining = 0;
    int total_frames = 1;
};

struct SokolUiAudioState {
    UiVoice move{};
    UiVoice click{};
    int sample_rate = 44100;
    bool ready = false;
};

SokolUiAudioState *g_audio_state = nullptr;

void start_voice(UiVoice &voice, float frequency, int frames) {
    voice.phase = 0.0f;
    voice.frequency = frequency;
    voice.frames_remaining = frames;
    voice.total_frames = std::max(1, frames);
}

float render_voice(UiVoice &voice, int sample_rate) {
    if (voice.frames_remaining <= 0) {
        return 0.0f;
    }

    const int elapsed = voice.total_frames - voice.frames_remaining;
    const float t = static_cast<float>(elapsed) / static_cast<float>(voice.total_frames);
    const float envelope = (1.0f - t) * (1.0f - t);
    const float sample = 0.24f * envelope * std::sin(voice.phase);
    voice.phase += (2.0f * kPi * voice.frequency) / static_cast<float>(sample_rate);
    voice.frames_remaining--;
    return sample;
}

void ui_audio_callback(float *buffer, int num_frames, int num_channels) {
    if (!g_audio_state || !g_audio_state->ready) {
        return;
    }

    for (int frame = 0; frame < num_frames; ++frame) {
        float sample = 0.0f;
        sample += render_voice(g_audio_state->move, g_audio_state->sample_rate);
        sample += render_voice(g_audio_state->click, g_audio_state->sample_rate);
        sample = std::clamp(sample, -0.8f, 0.8f);
        for (int ch = 0; ch < num_channels; ++ch) {
            buffer[frame * num_channels + ch] += sample;
        }
    }
}

} // namespace

struct UiAudio::Impl {
    SokolUiAudioState state{};
};

bool UiAudio::init() {
    if (ready) {
        return true;
    }

    impl = new (std::nothrow) Impl();
    if (!impl) {
        return false;
    }

    if (!saudio_isvalid()) {
        saudio_desc desc{};
        desc.stream_cb = ui_audio_callback;
        desc.logger.func = slog_func;
        saudio_setup(&desc);
        if (!saudio_isvalid()) {
            delete impl;
            impl = nullptr;
            return false;
        }
    }

    impl->state.sample_rate = saudio_sample_rate();
    impl->state.ready = true;
    g_audio_state = &impl->state;
    ready = true;
    return true;
}

void UiAudio::shutdown() {
    if (impl && g_audio_state == &impl->state) {
        g_audio_state = nullptr;
    }
    delete impl;
    impl = nullptr;
    ready = false;
}

void UiAudio::play_move() {
    if (!ready || !impl) {
        return;
    }
    start_voice(impl->state.move, 660.0f, impl->state.sample_rate / 18);
}

void UiAudio::play_click() {
    if (!ready || !impl) {
        return;
    }
    start_voice(impl->state.click, 980.0f, impl->state.sample_rate / 24);
}
