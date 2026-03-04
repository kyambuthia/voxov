#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include <glm/glm.hpp>

template <typename RemotePlayerT>
bool net_remote_sample_matches(
    const RemotePlayerT &remote,
    uint32_t server_tick,
    const glm::vec3 &position,
    const glm::vec3 &velocity,
    uint8_t anim_state,
    float anim_phase,
    float anim_blend) {
    if (remote.samples.empty()) {
        return false;
    }
    const typename RemotePlayerT::Sample &last = remote.samples.back();
    return last.position == position &&
           last.server_tick == server_tick &&
           last.velocity == velocity &&
           last.anim_state == anim_state &&
           last.anim_phase == anim_phase &&
           last.anim_blend == anim_blend;
}

template <typename RemotePlayerT>
void net_remote_push_sample(
    RemotePlayerT &remote,
    uint32_t server_tick,
    const glm::vec3 &position,
    const glm::vec3 &velocity,
    uint8_t anim_state,
    float anim_phase,
    float anim_blend,
    size_t sample_history_max) {
    if (net_remote_sample_matches(remote, server_tick, position, velocity, anim_state, anim_phase, anim_blend)) {
        return;
    }

    typename RemotePlayerT::Sample sample{};
    sample.server_tick = server_tick;
    sample.position = position;
    sample.velocity = velocity;
    sample.anim_state = anim_state;
    sample.anim_phase = anim_phase;
    sample.anim_blend = anim_blend;
    remote.samples.push_back(sample);
    while (remote.samples.size() > sample_history_max) {
        remote.samples.pop_front();
    }
}

template <typename RemoteMapT>
void net_remote_interpolate(
    RemoteMapT &remote_players,
    double &interp_tick_cursor,
    bool &interp_tick_cursor_initialized,
    double frame_dt,
    double fixed_dt,
    uint32_t interp_delay_ticks,
    float max_snap_error = 4.0f,
    float extrapolation_seconds_cap = 0.10f) {
    if (fixed_dt <= 0.0) {
        fixed_dt = 1.0 / 60.0;
    }

    uint32_t max_remote_sample_tick = 0;
    bool have_remote_samples = false;
    for (const auto &[player_id, remote] : remote_players) {
        (void)player_id;
        if (!remote.samples.empty()) {
            max_remote_sample_tick = std::max(max_remote_sample_tick, remote.samples.back().server_tick);
            have_remote_samples = true;
        }
    }

    if (have_remote_samples) {
        const double desired_tick = static_cast<double>(
            max_remote_sample_tick > interp_delay_ticks ? (max_remote_sample_tick - interp_delay_ticks) : 0u);
        if (!interp_tick_cursor_initialized) {
            interp_tick_cursor = desired_tick;
            interp_tick_cursor_initialized = true;
        } else {
            interp_tick_cursor += frame_dt / fixed_dt;
            if (interp_tick_cursor < (desired_tick - 20.0)) {
                interp_tick_cursor = desired_tick;
            }
            interp_tick_cursor = std::min(interp_tick_cursor, desired_tick + 2.0);
        }
    } else {
        interp_tick_cursor_initialized = false;
    }

    const float remote_lerp = std::clamp(static_cast<float>(frame_dt) * 12.0f, 0.0f, 1.0f);
    for (auto &[player_id, remote] : remote_players) {
        (void)player_id;
        const double target_tick_f = interp_tick_cursor_initialized
            ? interp_tick_cursor
            : static_cast<double>(remote.samples.empty() ? 0u : remote.samples.back().server_tick);

        while (remote.samples.size() >= 3 &&
               static_cast<double>(remote.samples[1].server_tick) <= target_tick_f) {
            remote.samples.pop_front();
        }

        glm::vec3 predicted_target = remote.target_position + remote.velocity * 0.035f;
        if (!remote.samples.empty()) {
            if (remote.samples.size() >= 2) {
                const auto &a = remote.samples[0];
                const auto &b = remote.samples[1];
                if (target_tick_f <= static_cast<double>(a.server_tick)) {
                    predicted_target = a.position;
                    remote.velocity = a.velocity;
                    remote.anim_state = a.anim_state;
                    remote.anim_phase = a.anim_phase;
                    remote.anim_blend = a.anim_blend;
                } else if (target_tick_f <= static_cast<double>(b.server_tick)) {
                    const float dt_ticks = static_cast<float>(std::max<uint32_t>(1u, b.server_tick - a.server_tick));
                    const float t = std::clamp(
                        static_cast<float>(target_tick_f - static_cast<double>(a.server_tick)) / dt_ticks,
                        0.0f,
                        1.0f);
                    predicted_target = glm::mix(a.position, b.position, t);
                    remote.velocity = glm::mix(a.velocity, b.velocity, t);
                    remote.anim_state = (t < 0.5f) ? a.anim_state : b.anim_state;
                    remote.anim_phase = glm::mix(a.anim_phase, b.anim_phase, t);
                    remote.anim_blend = glm::mix(a.anim_blend, b.anim_blend, t);
                } else {
                    const auto &latest = remote.samples.back();
                    const double ahead_ticks = std::max(0.0, target_tick_f - static_cast<double>(latest.server_tick));
                    const float extrap = std::clamp(static_cast<float>(ahead_ticks * fixed_dt), 0.0f, extrapolation_seconds_cap);
                    predicted_target = latest.position + latest.velocity * extrap;
                    remote.velocity = latest.velocity;
                    remote.anim_state = latest.anim_state;
                    remote.anim_phase = latest.anim_phase;
                    remote.anim_blend = latest.anim_blend;
                }
            } else {
                const auto &latest = remote.samples.back();
                predicted_target = latest.position;
                remote.velocity = latest.velocity;
                remote.anim_state = latest.anim_state;
                remote.anim_phase = latest.anim_phase;
                remote.anim_blend = latest.anim_blend;
            }
            remote.target_position = predicted_target;
        }

        const float err = glm::length(remote.position - predicted_target);
        if (err > max_snap_error) {
            remote.position = predicted_target;
        } else {
            remote.position = glm::mix(remote.position, predicted_target, remote_lerp);
        }
    }
}
