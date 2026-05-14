# VOXOV Handbook

VOXOV is a C++23 cross-platform game engine and multiplayer voxel world prototype built from one
repository. Its architecture follows the layered engine model described in Jason Gregory's *Game
Engine Architecture* (3rd Edition) — the canonical reference for how industrial-strength game
engines are structured.

## Quick Start

```
git clone --recurse-submodules github.com/kyambuthia/voxov.git
cd voxov
cmake -S . -B build/desktop/main -DVOXOV_BUILD_TESTS=ON
cmake --build build/desktop/main --parallel
./build/desktop/main/bin/voxov
```

## Current Shape

- **Desktop** (Linux/Windows/macOS): primary runtime via `GameRuntime` + platform adapters
- **Dedicated Server**: standalone authoritative server binary (`voxov_server`)
- **Android**: active native runtime (GLES, touch, multiplayer)
- **Web**: preview target (Emscripten/WebGL2)
- **iOS/Console/XR**: scaffolds, disabled by default

## Documentation Map

### Core Architecture
| Document | Purpose |
|---|---|
| [`ARCHITECTURE.md`](ARCHITECTURE.md) | Complete layered architecture with 16-subsystem GEA mapping |
| [`ROADMAP.md`](ROADMAP.md) | 10-phase implementation plan following GEA subsystem order |
| [`VOXOV_IMPLEMENTATION_GUIDE.md`](VOXOV_IMPLEMENTATION_GUIDE.md) | Per-subsystem implementation details and execution order |
| [`ENGINE_VISION.md`](ENGINE_VISION.md) | Long-term vision and design philosophy |

### Build & Run
| Document | Purpose |
|---|---|
| [`SETUP.md`](SETUP.md) | Development environment setup |
| [`BUILD_PLATFORMS.md`](BUILD_PLATFORMS.md) | Platform support matrix and build commands |
| [`RUNNING.md`](RUNNING.md) | Runtime flags, hotkeys, test commands |
| [`RELEASES.md`](RELEASES.md) | Release workflow and artifact publishing |

### Platform-Specific
| Document | Purpose |
|---|---|
| [`ANDROID.md`](ANDROID.md) | Android NDK build, Gradle packaging, multiplayer validation |
| [`WEB.md`](WEB.md) | Emscripten build, WASM runtime, browser transport integration |

### Subsystems
| Document | Purpose |
|---|---|
| [`NETWORKING.md`](NETWORKING.md) | Authoritative server model, packet types, LAN bring-up |
| [`DEBUGGING.md`](DEBUGGING.md) | Debug flags, hotkeys, Dear ImGui, profiling |
| [`EXTENDING.md`](EXTENDING.md) | How to extend the engine with new systems and targets |
| [`AVBD_INTEGRATION.md`](AVBD_INTEGRATION.md) | Experimental AVBD solver integration plan |
| [`VOXEL_WORLD_DESIGN.md`](VOXEL_WORLD_DESIGN.md) | Voxel planet terrain, LOD, streaming, generation design |

## Engine Architecture (30-Second Summary)

The engine is organized as a strict layered stack — upper layers depend on lower layers, never
the reverse:

```
Game-Specific (mechanics, cameras, weapons)
  └─ Gameplay Foundation (object model, events, scripting, world streaming)
       └─ Networking │ Animation │ Audio │ HID
            └─ Collision & Physics │ Profiling & Debugging
                 └─ Rendering Engine
                      └─ Resource Manager
                           └─ Core Systems (memory, math, strings, config)
                                └─ Platform Independence Layer
                                     └─ Third-Party SDKs (GLFW, ENet, Jolt, ImGui, GLM)
```

Every subsystem has a defined place. See [`ARCHITECTURE.md`](ARCHITECTURE.md) for the full
16-layer mapping with current source tree locations.

## Design Principles

These principles come from GEA and guide every implementation decision:

1. **Data-driven over hard-coded**: Behavior in data and scripts, not compiled C++.
2. **Composition over inheritance**: Game objects compose components; never deep class trees.
3. **Bulk updates over per-object iteration**: Subsystems update all instances of a type
   at once for cache coherence.
4. **Fixed-step simulation**: Physics and gameplay update at a fixed rate independent of
   render frame rate.
5. **Explicit startup/shutdown**: `startUp()`/`shutDown()` in code, never rely on global
   constructor ordering.
6. **Hashed string IDs**: 32/64-bit integers for runtime comparisons, not `strcmp`.
7. **RAII janitors**: Scope guards for allocators, mutexes, file handles.
8. **Middleware-first**: Use proven libraries (Jolt, ENet, cgltf, Dear ImGui). Build
   custom only when the problem is unique to your game.
9. **KISS**: Every feature must justify its build cost. Simplicity ships.

## Third-Party Dependencies

| Library | Role | Source |
|---|---|---|
| GLFW | Windowing, input, OpenGL context | CMake FetchContent |
| ENet | Reliable UDP transport | Git submodule |
| Jolt Physics | Rigid body dynamics, collisions | CMake FetchContent |
| Dear ImGui | Debug UI, in-game menus | CMake FetchContent |
| GLM | Math (vectors, matrices, quaternions) | Git submodule |
| cgltf | glTF model/animation loading | Bundled header |
| miniaudio | Cross-platform audio | Bundled header |
| stb_truetype | Font rasterization | Bundled header |
| fmt, spdlog | Formatting, logging | CMake FetchContent |

## Source Tree

```
src/
├── game/                    Entry points, runtime adapters, GameRuntime
├── engine/                  Transitional desktop orchestrator (being decomposed)
├── engine_core/             Timing, jobs, memory management
├── engine_math/             Camera, transforms
├── engine_input/            Platform-agnostic input abstraction
├── engine_render/           OpenGL renderer, debug text
├── engine_world/            Voxel terrain, world generation, chunk meshing
├── engine_physics/          Jolt integration, AVBD solver, vehicles
├── engine_net_proto/        Wire format types, protocol helpers
├── engine_net/              ENet client/server, LAN discovery, remote interp
├── engine_server/           Authoritative server session
├── engine_runtime/          Shared session state, world state
├── engine_presentation/     HUD composer, debug scene builder
├── engine_gameplay/         Player controller, animations, minigames
├── engine_assets/           glTF loading, skinned/static models
├── engine_audio/            UI audio, embedded sound data
├── engine_ui/               ImGui-based menus
├── engine_xr/               XR session scaffold
├── platform/                Platform abstraction layer
├── tests/                   Unit and integration tests
├── tools/                   Asset cooker
└── third_party/             Bundled single-header libraries
```

## Where We Are Now vs. Where We're Going

| System | Current | Target (GEA) |
|---|---|---|
| Memory | Heap allocations mixed in | Custom allocators (stack, pool, single-frame) |
| Strings | Raw `strcmp`/`strcpy` | Hashed string IDs with compile-time hashing |
| Config | CLI flags only | Console variables + config files + in-game console |
| Startup | Mixed constructors + `main()` | Explicit `startUp()`/`shutDown()` ordering |
| Resources | Ad-hoc file loading | Unified ResourceManager with GUID registry |
| Renderer | Single-pass OpenGL | Layered: GDI → materials → scene graph → culling → draw |
| Animation | Basic clip playback | Full blend trees + action state machine + IK |
| Physics | Jolt integrated, not deeply used | Full collision queries + ragdolls + constraints |
| Audio | Embedded UI sounds only | 3D positional audio + sound banks + streaming |
| Networking | Working ENet, basic prediction | Delta compression + interest management + NAT traversal |
| Object Model | System-specific containers | Component-based ECS with events and scripting |
| Platforms | Desktop + Android + Web preview | Single GameRuntime across all targets |

The roadmap (`ROADMAP.md`) and implementation guide (`VOXOV_IMPLEMENTATION_GUIDE.md`) describe
how to close these gaps in ordered phases.
