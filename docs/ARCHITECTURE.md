# VOXOV Architecture

This document describes the intended module boundaries and the data flow for the tiny
multiplayer demo and the future engine.

## Module Layout

- `platform/`: OS, windowing, input, filesystem, timing, threads.
- `render/`: renderer front-end, frame graph, materials, GPU resources.
- `sim/`: deterministic simulation and world state.
- `net/`: networking, replication, prediction, and snapshotting.
- `engine/`: high-level orchestration, lifecycle, and glue.

## Data Flow (High Level)

1. **Platform** collects input and timing.
2. **Net** exchanges input commands and snapshots with the server.
3. **Sim** runs fixed-timestep updates, applying local inputs or server snapshots.
4. **Render** reads the sim state (read-only) to build frames.
5. **Engine** orchestrates and enforces threading boundaries.

## Multiplayer Model

- **Authoritative server** owns the truth.
- **Clients** send input commands; server sends snapshots.
- **Prediction** for local player on the client.
- **Interpolation** for remote players.

## Demo Scope (First Cut)

- One world with a few entities (players + a few static objects).
- Simplified physics (capsule or AABB + ground plane).
- Simple replication: position, velocity, facing.
- Debug UI overlay for latency and tick rate.
