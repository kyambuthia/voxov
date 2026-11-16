#pragma once

#include "engine_gameplay/player/player_components.hpp"
#include "engine_net_proto/net_types.hpp"

#include <cmath>
#include <glm/glm.hpp>

enum class SnapshotReconcileAction {
    None,
    Blended,
    Snapped,
};

// Reconciles a locally predicted PlayerEntity against an authoritative server
// NetSnapshot using a small deadband, smooth correction, and snap threshold.
inline SnapshotReconcileAction net_reconcile_player_snapshot(
    PlayerEntity &player,
    const NetSnapshot &snapshot) {
    if (!std::isfinite(snapshot.x) || !std::isfinite(snapshot.y) ||
        !std::isfinite(snapshot.z) || !std::isfinite(snapshot.vx) ||
        !std::isfinite(snapshot.vy) || !std::isfinite(snapshot.vz)) {
        return SnapshotReconcileAction::None;
    }

    const glm::vec3 auth_pos(
        static_cast<float>(snapshot.x),
        static_cast<float>(snapshot.y),
        static_cast<float>(snapshot.z));
    const glm::vec3 auth_vel(snapshot.vx, snapshot.vy, snapshot.vz);
    const float error_m = glm::length(auth_pos - player.transform.position);

    constexpr float kDeadbandMeters = 0.15f;
    constexpr float kSnapMeters = 4.0f;
    constexpr float kBlendAlpha = 0.20f;

    if (error_m < kDeadbandMeters) {
        return SnapshotReconcileAction::None;
    }
    if (error_m >= kSnapMeters) {
        player.transform.position = auth_pos;
        player.controller.velocity = auth_vel;
        return SnapshotReconcileAction::Snapped;
    }
    player.transform.position =
        glm::mix(player.transform.position, auth_pos, kBlendAlpha);
    player.controller.velocity =
        glm::mix(player.controller.velocity, auth_vel, kBlendAlpha);
    return SnapshotReconcileAction::Blended;
}
