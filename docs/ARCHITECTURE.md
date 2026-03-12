# VOXOV Architecture

This document describes the architecture that is implemented today.

## Runtime Matrix

- Desktop: primary integrated runtime in `src/game/main.cpp`, backed by `Engine`
- Android: separate native runtime in `src/game/android_main.cpp`
- Web: preview runtime in `src/game/web_main.cpp`
- iOS, console, and XR: scaffolds only, disabled by default

The important distinction is that VOXOV is a shared codebase, but not yet a single shared runtime architecture across every target.

## Desktop Runtime

The desktop target is the cleanest path in the repository:

1. `src/game/main.cpp` parses CLI flags and creates `DesktopPlatform`
2. `Engine` owns the frame loop, fixed-step simulation, renderer, physics, and networking
3. `Renderer` selects Vulkan or OpenGL backends
4. Shared gameplay, world, and network code lives under `src/engine_*`

Core module boundaries:

- `engine_core/*`: timing and low-level helpers
- `engine_math/*`: camera and transforms
- `engine_render/*`: render abstractions plus Vulkan/OpenGL backends
- `engine_world/*`: voxel terrain and collision helpers
- `engine_physics/*`: physics backends and vehicle helpers
- `engine_net/*`: ENet client/server and LAN discovery
- `engine_ui/*`: in-game menu flow
- `engine_gameplay/*`: player, animation, minigames, and related gameplay code

## Target-Specific Paths

### Android

Android shares some lower-level modules such as world, gameplay, networking, and UI code, but it does not reuse the desktop `Engine` orchestration layer. The Android target runs its own native loop, rendering path, menu state, and networking/session flow.

### Web

The Web target is a lightweight preview. It currently implements a minimal local movement loop, menu handling, and optional JavaScript transport hooks rather than the full desktop runtime.

### Legacy and Future Scaffolds

The repository still contains:

- legacy SDL/Vulkan demo sources under `src/main.cpp`, `src/game.cpp`, `src/renderer/*`, and `src/world/*`
- future scaffolds for iOS, console, and XR

These paths are useful reference material, but they are not the main shipping runtime.

## What Is Designed Well

- The repository already has sensible module folders under `src/engine_*`
- The desktop runtime keeps rendering behind a backend abstraction
- Build outputs, tests, and release packaging are clearly represented in CMake and GitHub Actions
- Platform ambition is documented separately from the code, rather than being hidden in random source files

## Current Design Liabilities

### 1. `Engine` is too broad

`Engine` currently owns rendering, physics, local and remote player state, networking, UI, persistence, objectives, minigames, vehicle state, and split-screen flow. That makes the desktop runtime hard to test and hard to reuse from other targets.

### 2. Cross-platform behavior is only partially shared

Android and Web both bypass the desktop orchestration layer. That means gameplay, networking, and menu behavior can drift across targets even when they nominally ship from one repository.

### 3. The platform story is more aspirational than integrated

The repo contains iOS, console, and XR scaffolds, but only desktop is a fully integrated shared-engine runtime today. Planning docs should reflect that distinction clearly.

## Architectural Direction

The next architectural milestone should not be "add more platforms." It should be:

1. Extract shared session/gameplay/network subsystems from `Engine`
2. Reuse them from Android before expanding target scope
3. Keep Web intentionally small until a real shared runtime path exists

For execution priorities, see `docs/ROADMAP.md`.
