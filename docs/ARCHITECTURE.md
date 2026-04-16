# VOXOV Architecture

This document describes the architecture that is implemented today.

## Runtime Matrix

- Desktop: primary integrated runtime in `src/game/main.cpp`, booted through `GameRuntime` and desktop adapters, with `Engine` still providing most implementation
- Dedicated server: `src/game/server_main.cpp`, a standalone authoritative ENet server binary
- Android: separate native runtime in `src/game/android_main.cpp`
- Web: preview runtime in `src/game/web_main.cpp`
- iOS, console, and XR: scaffolds only, disabled by default

The important distinction is that VOXOV is a shared codebase with a real shared runtime seam on desktop, but not yet a single shared runtime architecture across every target.

## Desktop Runtime

The desktop target is the cleanest path in the repository:

1. `src/game/main.cpp` parses CLI flags, optionally starts an in-process `NetServer`, and creates `DesktopPlatform`
2. `DesktopRuntimePlatformAdapter` and `DesktopRuntimeInputAdapter` bridge platform services into `GameRuntime`
3. `GameRuntime` owns per-frame platform polling, menu/session flow handoff, and the runtime-facing shell API
4. `Engine` still owns most fixed-step simulation, renderer, physics, networking, and presentation assembly behind that shell
5. `Renderer` currently targets a single OpenGL desktop backend and keeps the render contract aligned with GLES3/WebGL2-class limits

Core module boundaries:

- `engine_core/*`: timing and low-level helpers
- `engine_math/*`: camera and transforms
- `engine_render/*`: render abstractions plus the active OpenGL backend
- `engine_world/*`: voxel terrain and collision helpers
- `engine_physics/*`: physics backends and vehicle helpers
- `engine_net_proto/*`: protocol types, headers, feature flags, and POD helpers
- `engine_net/*`: ENet client/server transport plus networking runtime helpers
- `engine_server/*`: authoritative server session/state
- `engine_ui/*`: in-game menu flow
- `engine_runtime/*`: shared runtime state and session helpers
- `engine_presentation/*`: HUD/debug snapshot building
- `engine_gameplay/*`: player, animation, minigames, and related gameplay code
- `game/*`: entry points, runtime adapters, and `GameRuntime`

## Target-Specific Paths

### Android

Android shares lower-level modules such as world, gameplay, networking, session helpers, and UI code, but it does not yet reuse the desktop `GameRuntime` path. The Android target still runs its own native loop, rendering path, and platform integration.

### Web

The Web target is a lightweight preview. It currently reuses the session/menu controller but still implements its own minimal movement loop and JavaScript transport hooks rather than the full desktop runtime. The intended direction is to move more gameplay and runtime code into WASM while keeping JavaScript limited to browser I/O, asset/bootstrap wiring, and browser-appropriate transport glue.

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

### 1. `Engine` is still too broad

`Engine` still owns rendering, physics, local and remote player state, networking, UI, persistence, objectives, minigames, vehicle state, and split-screen flow. Wrapping it behind `GameRuntime` improves entry-point shape, but it does not yet make those responsibilities modular.

### 2. The shared runtime seam is still shallow

Desktop now uses `GameRuntime`, but most runtime behavior still drops directly into `Engine`. The next refactor steps need to move real state ownership into `game/*`, `engine_runtime/*`, and `engine_presentation/*` rather than stopping at an adapter wrapper.

### 3. Cross-platform behavior is only partially shared

Android and Web still bypass the desktop `GameRuntime` path. That means gameplay, networking, and menu behavior can drift across targets even when they nominally ship from one repository.

## Shared Runtime Contract

The shared runtime contract now exists and looks like this:

1. `GameRuntime` exposes initialization, input handoff, per-frame ticking, shutdown, and connect entrypoints.
2. Platform entry points stay thin and own only lifecycle, native window/context setup, platform event pumping, and platform-specific input capture.
3. Desktop is already on that path; Android and Web are the next migrations.

Current ownership split:

- `src/game/*`: thin entry points plus `GameRuntime` and platform adapter interfaces
- `src/engine_runtime/*`: shared runtime state and session helpers
- `src/engine/*`: transitional desktop orchestrator that will shrink as runtime state moves into the shared layer
- `src/platform/*`: desktop/native platform adapters and services only

## Planned Subsystem Boundaries

The refactor target remains a small set of explicit subsystems instead of one large `Engine` orchestrator.

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

Networking is now split into separate layers and should stay that way:

- `engine_net_proto`: wire types, serializers, validators, and protocol flags only
- `engine_net_transport`: ENet transport only
- `engine_net_discovery`: LAN discovery only
- client runtime
- `engine_server`: server simulation/state

Protocol code must not depend on world generation, renderer types, or platform APIs.

### Presentation domain

Presentation should consume abstract scene and HUD/debug snapshots rather than owning
simulation state directly. Gameplay and runtime code may build render-facing data, but
shared runtime code must not depend on platform-specific renderer implementations.

## Planned Build Graph

The build graph is already moving toward a layered shape so shared code compiles once and is reused by every runtime.

1. foundation: `engine_core`, `engine_math`
2. domain: `engine_world`, `engine_physics`, `engine_server`, `engine_net`
3. gameplay/assets: `engine_gameplay`, `engine_assets`, `engine_audio`, `engine_ui`
4. orchestration: `engine_runtime`, `engine_presentation`
5. platform/apps: renderer backends, runtime entrypoints, and platform adapters

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

1. Keep the runtime and adapter interfaces stable while moving real ownership behind them.
2. Move more session/world/presentation state out of `Engine` and into shared runtime-facing modules.
3. Keep networking cleanly split across protocol, transport, discovery, client, and server-session layers.
4. Remove platform-only dependencies from shared render/runtime code.
5. Migrate Android and Web to thin adapters over the shared runtime.
6. Remove duplicate runtime paths after parity is established.

For execution priorities, see `docs/ROADMAP.md`.
