# VOXOV Architecture (Genesis Baseline)

## Architecture Tenet: True Cross-Platform

VOXOV must ship from one shared codebase across:

- Desktop: Linux, Windows, macOS
- Mobile: Android, iOS
- Consoles: PlayStation/Xbox/Nintendo platform targets
- XR/AR headsets: Quest/Oculus, PC VR headsets, Apple Vision Pro

Design implications:

- Platform code stays behind `platform/*` interfaces; engine/game logic remains platform-agnostic.
- Rendering abstraction supports backend/platform surface differences without gameplay forks.
- Input, file IO, threading, timing, networking, and save paths use engine abstractions, not ad-hoc platform calls.
- New engine systems are accepted only if they can map to all target platform classes or include a documented fallback path.

## XR/AR Tenet

XR/AR is a first-class platform target, not a post-port.

- Quest/Oculus and PC VR target an OpenXR path.
- Apple Vision Pro targets a visionOS-specific path.
- Gameplay/simulation remains shared and headset-agnostic.
- XR-specific logic lives in platform/render/input layers (stereo views, pose tracking, motion controllers, frame timing).
- Comfort and frame pacing are mandatory quality gates for headset builds.

## Gameplay Tenet: Multi-Modal Traversal

Core gameplay must support seamless traversal via:

- Walking on terrain
- Driving land vehicles (cars)
- Flying aircraft both within a planet and between planets

Design implications:

- Movement/controller architecture must support mode switching without duplicating netcode or camera stacks.
- Physics uses a shared authority model with mode-specific tuning (capsule, wheeled, aircraft) under one replication protocol.
- Streaming and LOD systems must prioritize content along current traversal velocity (ground and high-speed flight profiles).

## Runtime layers

- `platform/*`: window/context/input/time abstraction
- `engine_core/*`: timing/jobs/memory
- `engine_math/*`: transforms + camera
- `engine_world/*`: voxel data + chunk meshing
- `engine_render/*`: renderer API + Vulkan/GL backends
- `engine_xr/*`: XR session, stereo camera state, action bindings, and compositor-facing frame flow
- `engine_ui/*`: renderer-agnostic in-game GUI/menu state
- `engine_net/*`: ENet transport + channels + replication primitives
- `engine_physics/*`: Jolt world step
- `engine/*`: orchestration and fixed-timestep loop
- `game/main.cpp`: app bootstrap and CLI

Current scaffold note:

- `src/engine_xr/xr_session.*` is present as the baseline XR session scaffold.

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
- vehicle and aircraft placeholder actors for traversal mode integration
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
