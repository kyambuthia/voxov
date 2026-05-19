#pragma once

#include <cstdint>
#include <variant>

#include <glm/glm.hpp>

struct EngineStartedEvent {
    uint64_t frame = 0;
};

struct EngineStoppingEvent {
    uint64_t frame = 0;
};

struct FixedTickStartedEvent {
    uint64_t tick = 0;
    float dt = 0.0f;
};

struct FixedTickFinishedEvent {
    uint64_t tick = 0;
    float dt = 0.0f;
};

struct FrameStartedEvent {
    uint64_t frame = 0;
    float dt = 0.0f;
    float alpha = 0.0f;
};

struct FrameFinishedEvent {
    uint64_t frame = 0;
    float dt = 0.0f;
};

struct InputActionEvent {
    uint32_t player_id = 0;
    uint32_t action = 0;
    bool pressed = false;
};

struct PlayerSpawnedEvent {
    uint32_t player_id = 0;
    glm::vec3 position{0.0f};
};

struct PlayerDespawnedEvent {
    uint32_t player_id = 0;
};

struct PlayerMovedEvent {
    uint32_t player_id = 0;
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
};

struct PlayerJumpedEvent {
    uint32_t player_id = 0;
    glm::vec3 position{0.0f};
};

struct PlayerLandedEvent {
    uint32_t player_id = 0;
    glm::vec3 position{0.0f};
    float impact_speed = 0.0f;
};

struct PlayerHealthChangedEvent {
    uint32_t player_id = 0;
    float previous_health = 0.0f;
    float current_health = 0.0f;
};

struct VehicleSpawnedEvent {
    uint32_t vehicle_id = 0;
    glm::vec3 position{0.0f};
};

struct VehicleDestroyedEvent {
    uint32_t vehicle_id = 0;
};

struct VehicleDamagedEvent {
    uint32_t vehicle_id = 0;
    float damage = 0.0f;
    glm::vec3 position{0.0f};
};

struct CollisionEvent {
    uint32_t entity_a = 0;
    uint32_t entity_b = 0;
    glm::vec3 point{0.0f};
    glm::vec3 normal{0.0f};
    float impulse = 0.0f;
};

struct VoxelChangedEvent {
    glm::ivec3 position{0};
    uint8_t previous_material = 0;
    uint8_t current_material = 0;
};

struct ChunkLoadedEvent {
    int32_t chunk_x = 0;
    int32_t chunk_z = 0;
};

struct ChunkUnloadedEvent {
    int32_t chunk_x = 0;
    int32_t chunk_z = 0;
};

struct NetworkClientConnectedEvent {
    uint32_t client_id = 0;
};

struct NetworkClientDisconnectedEvent {
    uint32_t client_id = 0;
    uint32_t reason = 0;
};

struct NetworkSnapshotReceivedEvent {
    uint32_t client_id = 0;
    uint64_t tick = 0;
};

struct AudioCueRequestedEvent {
    uint32_t cue_id = 0;
    glm::vec3 position{0.0f};
};

struct HudMessageEvent {
    uint32_t message_id = 0;
    float duration_seconds = 0.0f;
};

using Event = std::variant<
    EngineStartedEvent,
    EngineStoppingEvent,
    FixedTickStartedEvent,
    FixedTickFinishedEvent,
    FrameStartedEvent,
    FrameFinishedEvent,
    InputActionEvent,
    PlayerSpawnedEvent,
    PlayerDespawnedEvent,
    PlayerMovedEvent,
    PlayerJumpedEvent,
    PlayerLandedEvent,
    PlayerHealthChangedEvent,
    VehicleSpawnedEvent,
    VehicleDestroyedEvent,
    VehicleDamagedEvent,
    CollisionEvent,
    VoxelChangedEvent,
    ChunkLoadedEvent,
    ChunkUnloadedEvent,
    NetworkClientConnectedEvent,
    NetworkClientDisconnectedEvent,
    NetworkSnapshotReceivedEvent,
    AudioCueRequestedEvent,
    HudMessageEvent>;
