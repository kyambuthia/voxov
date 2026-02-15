# VOXOV Architecture (Genesis Baseline)

## Runtime layers

- `platform/*`: window/context/input/time abstraction
- `engine_core/*`: timing/jobs/memory
- `engine_math/*`: transforms + camera
- `engine_world/*`: voxel data + chunk meshing
- `engine_render/*`: renderer API + Vulkan/GL backends
- `engine_ui/*`: renderer-agnostic in-game GUI/menu state
- `engine_net/*`: ENet transport + channels + replication primitives
- `engine_physics/*`: Jolt world step
- `engine/*`: orchestration and fixed-timestep loop
- `game/main.cpp`: app bootstrap and CLI

## Frame flow

1. Platform polls input/events.
2. Engine advances fixed simulation tick(s).
3. Net client sends input, receives snapshot/chunk updates.
4. Renderer draws uploaded scene with active backend.
5. Debug stats (FPS/CPU ms) are updated each frame.

## Scene baseline

- voxel terrain chunk (naive mesh)
- sky/atmosphere placeholder mesh
- debug ground grid mesh
- third-person player capsule + camera rig (yaw/pitch orbit, distance clamp, occlusion test)
- in-world debug overlays (dev HUD text + debug capsules/markers)

## Networking baseline

- authoritative server mode
- reliable and unreliable channels
- player assignment + per-player state replication (server -> all clients)
- chunk interest request and chunk state response

## Diagnostics baseline

- Vulkan validation layers + debug callback
- headless server mode
- `--devhud` structured runtime telemetry for input/camera/collision/network
- `--noclip` debug-only comparison mode
- unit tests: camera math, net serialization, chunk meshing
