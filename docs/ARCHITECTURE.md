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
3. `Renderer` currently targets a single OpenGL desktop backend and keeps the render contract aligned with GLES3/WebGL2-class limits
4. Shared gameplay, world, and network code lives under `src/engine_*`

Core module boundaries:

- `engine_core/*`: timing and low-level helpers
- `engine_math/*`: camera and transforms
- `engine_render/*`: render abstractions plus the active OpenGL backend
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
- The desktop runtime keeps rendering behind a backend abstraction even after converging on one backend
- Build outputs, tests, and release packaging are clearly represented in CMake and GitHub Actions
- Platform ambition is documented separately from the code, rather than being hidden in random source files

## Current Design Liabilities

### 1. `Engine` is too broad

`Engine` currently owns rendering, physics, local and remote player state, networking, UI, persistence, objectives, minigames, vehicle state, and split-screen flow. That makes the desktop runtime hard to test and hard to reuse from other targets.

### 2. Cross-platform behavior is only partially shared

Android and Web both bypass the desktop orchestration layer. That means gameplay, networking, and menu behavior can drift across targets even when they nominally ship from one repository.

### 3. The platform story is more aspirational than integrated

The repo contains iOS, console, and XR scaffolds, but only desktop is a fully integrated shared-engine runtime today. Planning docs should reflect that distinction clearly.

## Target Runtime Contract

The next milestone is to converge on one shared runtime contract before doing broader
platform work. The target shape is:

1. `GameRuntime` owns fixed-step advancement, session state, input handoff, and
   shared gameplay orchestration.
2. Platform entry points stay thin and own only lifecycle, native window/context setup,
   platform event pumping, and platform-specific input capture.
3. Desktop migrates first, then Android and Web are moved onto the same runtime path.

Target ownership split:

- `src/game/*`: thin entry points plus `GameRuntime` and platform adapter interfaces
- `src/engine_runtime/*`: shared runtime state and orchestration, cleaned up so it does
  not directly own UI, platform, or renderer concerns
- `src/engine/*`: transitional desktop orchestrator that will shrink as runtime state
  moves into the shared layer
- `src/platform/*`: platform adapters and native services only

## Planned Subsystem Boundaries

The refactor target is a small set of explicit subsystems instead of one large `Engine`
orchestrator.

### Runtime orchestration

Shared runtime code should own:

- fixed-step progression
- session/menu state transitions
- input distribution to simulation
- coordination between gameplay, networking, persistence, and presentation

### Gameplay domain

Gameplay code should own:

- local player simulation
- vehicles and aircraft
- minigames and objective interactions
- gameplay-facing state transitions that do not depend on platform APIs

### Networking domain

Networking should be split into separate layers:

- `engine_net_proto`: wire types, serializers, validators, and protocol flags only
- `engine_net_transport`: ENet transport only
- `engine_net_discovery`: LAN discovery only
- client runtime
- server simulation

Protocol code must not depend on world generation, renderer types, or platform APIs.

### Presentation domain

Presentation should consume abstract scene and HUD/debug snapshots rather than owning
simulation state directly. Gameplay and runtime code may build render-facing data, but
shared runtime code must not depend on platform-specific renderer implementations.

## Planned Build Graph

The target graph is layered so shared code compiles once and is reused by every runtime.

1. foundation: `engine_core`, `engine_math`
2. domain: `engine_world`, `engine_physics`, `engine_server`, `engine_net`
3. gameplay/assets: `engine_gameplay`, `engine_assets`, `engine_audio`, `engine_ui`
4. orchestration: `engine_runtime`, `engine_presentation`
5. platform: renderer backends and platform adapters

Key rules:

- shared targets must not depend on GLFW, EGL, Emscripten, or other platform headers
- platform executables link the platform adapter and renderer backend they need
- tests link shared libraries only; they should not compile Android or Web platform code

## Refactor Comment Policy

The refactor should add comments only where the control flow or invariants are not
obvious from the code. In practice that means:

- fixed-step advancement and accumulator behavior
- prediction/reconciliation ordering and buffer assumptions
- protocol and wire-format invariants
- JNI, EGL, or browser lifecycle constraints
- cached platform metadata or cross-platform state synchronization assumptions

Do not add explanatory comments for straightforward data movement or simple setter/getter
logic.

## Execution Priorities

1. Freeze the runtime and adapter interfaces.
2. Extract missing shared library targets without changing behavior.
3. Move desktop onto the shared runtime first.
4. Split networking into protocol, transport, discovery, client, and server-sim layers.
5. Remove platform-only dependencies from shared render/runtime code.
6. Migrate Android and Web to thin adapters over the shared runtime.
7. Remove duplicate runtime paths after parity is established.

For execution priorities, see `docs/ROADMAP.md`.
