#include "engine_audio/ui_audio.hpp"

#if defined(VOXOV_PLATFORM_ANDROID)

// Android uses its own audio path. Stub out sokol audio.
bool UiAudio::init()     { return false; }
void UiAudio::shutdown() {}
void UiAudio::play_move() {}
void UiAudio::play_click() {}
void UiAudio::play_block_break() {}
void UiAudio::play_block_place() {}
void UiAudio::play_footstep(uint8_t) {}

#else

#include "sokol_audio.h"
#include "sokol_log.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <new>

namespace {

constexpr float kPi = 3.14159265358979323846f;

struct UiVoice {
    float phase = 0.0f;
    float frequency = 440.0f;
    float amplitude = 0.24f;
    float noise_mix = 0.0f;
    int frames_remaining = 0;
    int total_frames = 1;
};

struct SokolUiAudioState {
    std::mutex mutex{};
    UiVoice move{};
    UiVoice click{};
    uint32_t rng = 0x12345678u;
    int sample_rate = 44100;
    bool ready = false;
};

SokolUiAudioState *g_audio_state = nullptr;

float next_noise(uint32_t &rng) {
    rng = rng * 1664525u + 1013904223u;
    return static_cast<float>(static_cast<int32_t>(rng >> 8)) / 8388608.0f;
}

void start_voice(UiVoice &voice, float frequency, int frames,
                 float amplitude = 0.22f, float noise_mix = 0.0f) {
    voice.phase = 0.0f;
    voice.frequency = frequency;
    voice.amplitude = amplitude;
    voice.noise_mix = noise_mix;
    voice.frames_remaining = frames;
    voice.total_frames = std::max(1, frames);
}

float render_voice(UiVoice &voice, int sample_rate, uint32_t &rng) {
    if (voice.frames_remaining <= 0) {
        return 0.0f;
    }

    const int elapsed = voice.total_frames - voice.frames_remaining;
    const float t = static_cast<float>(elapsed) / static_cast<float>(voice.total_frames);
    const float envelope = (1.0f - t) * (1.0f - t);
    const float tone = std::sin(voice.phase);
    const float noise = (voice.noise_mix > 0.0f) ? next_noise(rng) : 0.0f;
    const float mixed = tone * (1.0f - voice.noise_mix) + noise * voice.noise_mix;
    const float sample = voice.amplitude * envelope * mixed;
    voice.phase += (2.0f * kPi * voice.frequency) / static_cast<float>(sample_rate);
    voice.frames_remaining--;
    return sample;
}

void ui_audio_callback(float *buffer, int num_frames, int num_channels) {
    const int total_samples = num_frames * num_channels;
    if (!g_audio_state || !g_audio_state->ready) {
        std::fill_n(buffer, total_samples, 0.0f);
        return;
    }

    std::unique_lock<std::mutex> lock(g_audio_state->mutex, std::try_to_lock);
    if (!lock.owns_lock()) {
        std::fill_n(buffer, total_samples, 0.0f);
        return;
    }

    for (int frame = 0; frame < num_frames; ++frame) {
        float sample = 0.0f;
        sample += render_voice(g_audio_state->move, g_audio_state->sample_rate, g_audio_state->rng);
        sample += render_voice(g_audio_state->click, g_audio_state->sample_rate, g_audio_state->rng);
        sample = std::clamp(sample, -0.8f, 0.8f);
        for (int ch = 0; ch < num_channels; ++ch) {
            // sokol_audio stream_cb buffers are not guaranteed to be zeroed.
            buffer[frame * num_channels + ch] = sample;
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
    if (impl) {
        {
            std::lock_guard<std::mutex> lock(impl->state.mutex);
            impl->state.ready = false;
        }
        if (saudio_isvalid()) {
            saudio_shutdown();
        }
        if (g_audio_state == &impl->state) {
            g_audio_state = nullptr;
        }
    }
    delete impl;
    impl = nullptr;
    ready = false;
}

void UiAudio::play_move() {
    if (!ready || !impl) {
        return;
    }
    std::lock_guard<std::mutex> lock(impl->state.mutex);
    start_voice(impl->state.move, 660.0f, impl->state.sample_rate / 18);
}

void UiAudio::play_click() {
    if (!ready || !impl) {
        return;
    }
    std::lock_guard<std::mutex> lock(impl->state.mutex);
    start_voice(impl->state.click, 980.0f, impl->state.sample_rate / 24);
}

void UiAudio::play_block_break() {
    if (!ready || !impl) return;
    std::lock_guard<std::mutex> lock(impl->state.mutex);
    start_voice(impl->state.click, 140.0f, impl->state.sample_rate / 18, 0.16f, 0.55f);
}

void UiAudio::play_block_place() {
    if (!ready || !impl) return;
    std::lock_guard<std::mutex> lock(impl->state.mutex);
    start_voice(impl->state.move, 220.0f, impl->state.sample_rate / 26, 0.14f, 0.25f);
}

void UiAudio::play_footstep(uint8_t material_id) {
    if (!ready || !impl) return;
    std::lock_guard<std::mutex> lock(impl->state.mutex);
    const float base_hz = 95.0f + static_cast<float>(material_id % 6u) * 14.0f;
    start_voice(impl->state.move, base_hz, impl->state.sample_rate / 32, 0.08f, 0.65f);
}

#endif
