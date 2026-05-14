# VOXOV Architecture

This document defines the target architecture for VOXOV, grounded in the layered engine model
described in Jason Gregory's *Game Engine Architecture* (3rd Edition). Every subsystem has a
defined place in the stack, with strict upward-only dependencies.

## Layered Architecture Model

```
┌──────────────────────────────────────────────────────────────┐
│ 16. GAME-SPECIFIC SUBSYSTEMS                                 │
│     Player mechanics, cameras, weapons, vehicles, puzzles     │
├──────────────────────────────────────────────────────────────┤
│ 15. GAMEPLAY FOUNDATION SYSTEMS                              │
│     Object model, event system, scripting, world streaming,   │
│     high-level game flow (FSM)                                │
├──────────────────────────────────────────────────────────────┤
│ 14. NETWORKING │ 13. AUDIO │ 12. ANIMATION │ 11. HID        │
├──────────────────────────────────────────────────────────────┤
│ 10. COLLISION & PHYSICS │ 9. PROFILING & DEBUGGING           │
├──────────────────────────────────────────────────────────────┤
│ 8. RENDERING ENGINE                                          │
│     Low-level renderer, scene graph/culling, visual effects,  │
│     front end (HUD/GUI)                                       │
├──────────────────────────────────────────────────────────────┤
│ 7. RESOURCE MANAGER                                          │
│     Asset loading, lifetime management, cross-references      │
├──────────────────────────────────────────────────────────────┤
│ 6. CORE SYSTEMS                                              │
│     Assertions, memory management, math library, containers,  │
│     strings (hashed IDs), engine configuration                │
├──────────────────────────────────────────────────────────────┤
│ 5. PLATFORM INDEPENDENCE LAYER                               │
│     Wraps OS/hardware APIs: files, threads, time, rendering   │
├──────────────────────────────────────────────────────────────┤
│ 4. THIRD-PARTY SDKs & MIDDLEWARE                             │
│     GLFW, ENet, Jolt Physics, Dear ImGui, GLM, cgltf, etc.   │
├──────────────────────────────────────────────────────────────┤
│ 3. OPERATING SYSTEM                                          │
│ 2. DEVICE DRIVERS                                            │
│ 1. TARGET HARDWARE                                           │
│     Desktop (Linux/Windows/macOS), Mobile (Android/iOS), Web  │
└──────────────────────────────────────────────────────────────┘
```

**Dependency rule**: Upper layers depend on lower layers. Lower layers never depend on upper
layers. Circular dependencies are forbidden.

## Layer Mapping: GEA → VOXOV Source Tree

### Layer 1-3: Hardware, Drivers, OS
Not part of the engine source. Consumed through the Platform Independence Layer.

### Layer 4: Third-Party SDKs and Middleware

| Dependency | Role | Source |
|---|---|---|
| **GLFW** | Windowing, OpenGL context, input | `dependencies/` via CMake FetchContent |
| **ENet** | Reliable UDP transport | `dependencies/enet/` |
| **Jolt Physics** | Rigid body dynamics, collision detection | `dependencies/` via CMake |
| **Dear ImGui** | In-game debug UI, menus | `dependencies/` via CMake |
| **GLM** | Math library (vectors, matrices, quaternions) | `dependencies/glm/` |
| **cgltf** | glTF model/animation loading | `dependencies/cgltf/` |
| **miniaudio** | Cross-platform audio playback | `dependencies/miniaudio/` |
| **stb_truetype** | Font rasterization | `src/third_party/` |
| **fmt, spdlog** | String formatting, logging | `dependencies/` via CMake |

### Layer 5: Platform Independence Layer
`src/platform/` — wraps platform-specific APIs behind consistent interfaces.

| Module | Purpose |
|---|---|
| `platform.hpp` | Base platform interface: window creation, event pump, time queries |
| `desktop/` | GLFW-based window, input, and context management |
| `android_platform.*` | Android NativeActivity, EGL context, touch input |
| `web_platform.*` | Emscripten canvas, WebGL2 context, browser events |
| `ios_platform.*` | scaffold |
| `console_platform.*` | scaffold |
| `platform_services.*` | Save paths, asset roots, environment discovery |

### Layer 6: Core Systems
`src/engine_core/` — timing, jobs, memory. `src/engine_math/` — camera, transforms.

| Module | File(s) | Purpose |
|---|---|---|
| `engine_core` | `memory.cpp` | Custom allocators (stack, pool, aligned) |
| `engine_core` | — | Job system (future: fiber/coroutine-based) |
| `engine_math` | `camera.*` | View/projection transforms, orbit/fly cameras |
| `engine_input` | `input_state.hpp` | Platform-agnostic input abstraction |
| — | — | Hashed string IDs (future) |
| — | — | Engine configuration/console variables (future) |

**GEA principle**: Never allocate from the heap in a tight loop. Custom allocators (stack for
level loads, pool for fixed-size objects, single-frame for per-tick temporaries) are mandatory
for runtime performance.

### Layer 7: Resource Manager
Currently fragmented — no unified resource manager exists. Assets are loaded ad-hoc by
individual subsystems.

**Target architecture** (GEA §7.2):
- Single `ResourceManager` singleton with GUID-keyed registry
- Reference-counted lifetimes (global → level → temporary)
- Cross-reference resolution (GUIDs or pointer fix-up tables)
- Asset conditioning pipeline (DCC → intermediate format → platform-specific)

### Layer 8: Rendering Engine
`src/engine_render/` — OpenGL-first backend, aligned to GLES3/WebGL2 ceiling.

| Module | Purpose |
|---|---|
| `renderer.hpp` | Abstract renderer interface |
| `gl_renderer.*` | OpenGL/GLES3 implementation |
| `render_types.hpp` | Shared types: mesh, material, vertex format |
| `debug_text.*` | In-world debug text rendering |

**GEA rendering pipeline structure** (target):
1. **Low-Level Renderer** — graphics device interface (buffer management, shader compilation,
   draw calls), material/shader system, viewport management
2. **Scene Graph / Culling** — spatial subdivision (octree for voxel world), frustum culling,
   occlusion culling, draw submission
3. **Visual Effects** — particles, decals, post effects
4. **Front End** — HUD, in-game menus, debug overlays

**Current state**: Voxel terrain renders through a single pass. No scene graph, no material
system, no effect framework. The renderer is straightforward OpenGL with grid-based terrain
meshing.

**GEA rendering order** (to implement):
1. Z-prepass (front-to-back, z-only)
2. Opaque pass (sort by material, full shading)
3. Transparent pass (back-to-front, alpha blending)
4. Sky rendering
5. Post effects
6. HUD / overlays

### Layer 9: Profiling & Debugging
`src/engine_presentation/` — HUD composer, debug scene builder.

| Module | Purpose |
|---|---|
| `debug_scene_builder.*` | Build debug visualization geometry |
| `hud_composer.*` | Compose runtime HUD elements |
| `presentation_snapshot.hpp` | Snapshottable debug state |

**GEA debug tools** (target):
- In-game console with command history and auto-complete (via Dear ImGui)
- Debug drawing (lines, spheres, boxes, text)
- In-game profiling overlay (frame time, subsystem times, memory, network stats)
- Screenshot and movie capture
- Cheat system

### Layer 10: Collision & Physics
`src/engine_physics/` — Jolt Physics integration.

| Module | Purpose |
|---|---|
| `physics_world.*` | Jolt physics world initialization and stepping |
| `jolt_solver.*` | Jolt-specific solver configuration |
| `avbd_solver.*` | Experimental AVBD solver (alternative backend) |
| `physics_solver.hpp` | Solver abstraction interface |
| `voxel/` | Voxel-to-physics bridge (chunk shape extraction) |
| `vehicle/` | Vehicle physics: drivetrain, damage, aircraft, ground controller |

**GEA collision pipeline** (Jolt provides):
1. **Broad phase** — sweep and prune on AABBs
2. **Midphase** — compound shape BVH traversal
3. **Narrow phase** — GJK/EPA for convex shapes
4. **Collision response** — impulsive resolution, constraint solving

**GEA principle**: Physics simulation should run at a fixed time step (independent of render
frame rate). The physics world is a private data structure — game objects link to physics bodies
indirectly through component handles.

### Layer 11: Human Interface Devices (HID)
`src/engine_input/`, `src/platform/desktop/input_desktop.*`

| Module | Purpose |
|---|---|
| `input_state.hpp` | Platform-agnostic input state struct |
| `input_desktop.*` | GLFW keyboard/mouse → input state |
| `input_android.cpp` | Android touch → input state |

**GEA HID features** (target):
- Dead zone processing for analog sticks
- Button debouncing and edge detection (pressed/released events)
- Action mapping (remappable controls, "Jump" → Space or A button)
- Chord/sequence/gesture detection
- Force feedback / rumble (future)

### Layer 12: Animation
`src/engine_gameplay/animation/`, `src/engine_assets/`

| Module | Purpose |
|---|---|
| `animation_runtime.*` | Animation state machine, blend tree evaluation |
| `animation_types.*` | Enum definitions, parameter structs |
| `player_animation_graph.*` | Player-specific animation blend graph |
| `skeletal_animator.*` | Procedural skeleton pose generation |
| `skinned_model.*` | glTF skinned mesh loading and playback |

**GEA animation pipeline** (target):
1. Clip decompression + pose extraction
2. Pose blending (LERP, additive) via blend trees
3. Global pose generation (hierarchy walk: local → global)
4. Post-processing (IK, ragdoll driven, procedural)
5. Matrix palette generation for skinning

**Current state**: Dual path — glTF skinned mesh playback (via cgltf) and procedural
skeleton for debug. Animation states networked as parameters (NOT raw bone transforms).
Blend trees not yet implemented; basic weighted-average blending only.

**GEA principle**: Storing only joints (not bones) in the skeleton. Using SRT (Scale-Rotation-
Translation) format for poses — enables clean interpolation. Skeleton index ordering is
depth-first (child after parent in array — ensures parent matrices are computed before
children during the hierarchy walk).

### Layer 13: Audio
`src/engine_audio/`

| Module | Purpose |
|---|---|
| `ui_audio.*` | UI sound effects (click, move) |
| `ui_sound_data.hpp` | Embedded WAV data arrays |

**GEA audio architecture** (target):
- Voice bus model: Codec → Gain → Filters → Pan → Master Mixer
- 3D sound sources with distance attenuation and spatialization
- Listener attached to camera
- Sound banks for loading/unloading groups of assets
- Streaming for music and long dialog

**Current state**: Minimal — embedded UI sounds only. No 3D audio, no streaming, no
sound banks. miniaudio is included as a dependency but not fully integrated.

### Layer 14: Online Multiplayer / Networking
`src/engine_net/`, `src/engine_net_proto/`, `src/engine_server/`

| Module | Purpose |
|---|---|
| `net_common.hpp` | Protocol types, packet structs, channel enums |
| `net_protocol_helpers.hpp` | Serialization, validation helpers |
| `net_types.hpp` | Type definitions for wire format |
| `net_client.*` | ENet client: connect, send input, receive snapshots |
| `net_server.*` | ENet server: accept connections, simulate, broadcast |
| `net_runtime_shared.hpp` | Shared client/server runtime utilities |
| `remote_interp.hpp` | Remote player snapshot interpolation buffer |
| `lan_discovery.*` | UDP broadcast LAN host discovery |
| `server_session.*` | Authoritative server session state |

**GEA networking architecture** (target):
- **Protocol layer** (`engine_net_proto`): Wire types, serializers, validators, feature flags.
  Must NOT depend on world generation, renderer types, or platform APIs.
- **Transport layer** (`engine_net`): ENet UDP transport only.
- **Discovery layer**: LAN broadcast discovery.
- **Server simulation**: Authoritative tick-based simulation, independent of renderer.

**GEA principle**: Build multiplayer in from day one. Single-player should be a special case
of multiplayer (client-on-top-of-server). This avoids costly refactors later.

**Current state**: Authoritative server model with ENet. Client-side prediction and
reconciliation are implemented. Remote snapshot interpolation works on desktop and
Android. Protocol has framing, versioning, and sequence numbers. LAN discovery works.
Key gaps: no interest management, no delta compression, limited bandwidth optimization.

### Layer 15: Gameplay Foundation Systems
`src/engine_gameplay/`, `src/engine_runtime/`, `src/engine_ui/`

| Module | Purpose |
|---|---|
| `player/` | Player controller, physics, visuals |
| `minigames/` | In-world minigame logic |
| `runtime_game_session.*` | Session state, stepping |
| `runtime_session_controller.*` | Session lifecycle management |
| `runtime_world_state.*` | World state management, chunk streaming |
| `gui_menu.*` | In-game menu system (ImGui-based) |

**GEA gameplay foundation subsystems** (target):
1. **Game World & Object Model**: Component-based object model (see §16.2 of GEA).
   Current VOXOV uses a partially component-oriented approach through
   `PlayerControllerSystem` and `VoxelCollisionWorld`, but lacks a unified game object
   model.
2. **Event System**: Inter-object communication via typed events with message queues.
   Not yet implemented — current communication is direct function calls.
3. **Scripting System**: Lua or similar for data-driven behavior. Not yet implemented.
4. **World Streaming**: Chunk-based loading around player interest zones. Basic
   implementation exists via `NetChunkInterest`/`NetChunkState`.

**GEA component model** (target for VOXOV):
- Game objects are containers holding component instances
- Components own specific concerns: `TransformComponent`, `MeshComponent`,
  `RigidBodyComponent`, `AnimationComponent`, `AudioComponent`
- Components are updated in bulk by their owning subsystem (better cache coherence than
  per-object update loops)
- Phased updates: `PreAnimUpdate()` → `PostAnimUpdate()` → `FinalUpdate()`

### Layer 16: Game-Specific Subsystems
`src/game/` — entry points, runtime adapters, game-specific logic.

| Module | Purpose |
|---|---|
| `game_runtime.*` | Runtime-facing shell, session orchestration |
| `desktop_runtime_adapter.*` | Desktop platform adapter |
| `runtime_adapters.hpp` | Adapter interface definitions |
| `runtime_session_flow.*` | Session state machine (menu → game → menu) |
| `main.cpp` | Desktop entry point |
| `server_main.cpp` | Dedicated server entry point |
| `android_main.cpp` | Android entry point |
| `web_main.cpp` | Web entry point |
| `web_session_flow.*` | Web-specific session flow |

## Dependency Graph

```
game ──────────────────────────────────────────────┐
  │ (entry points, runtime adapters, session flow)  │
  ▼                                                 │
engine_runtime ──────────────────────────────────┐  │
  │ (session, world state, session controller)   │  │
  ▼                                              │  │
engine_gameplay ─────┐                           │  │
engine_presentation ─┤                           │  │
engine_ui ───────────┤                           │  │
engine_assets ───────┤                           │  │
engine_audio ────────┤                           │  │
  │                   │                          │  │
  ▼                   ▼                          ▼  ▼
engine_net ─────── engine_physics ── engine_render
engine_net_proto ── engine_world ─── engine_server
  │                   │                          │
  ▼                   ▼                          ▼
engine_core ─────── engine_math ──── engine_input
  │
  ▼
platform ── third-party ── OS ── hardware
```

**Key rules**:
- Shared libraries must NOT depend on GLFW, EGL, Emscripten, or other platform headers
- Protocol code must NOT depend on world generation, renderer types, or platform APIs
- Platform executables link the platform adapter and renderer backend they need
- Tests link shared libraries only; they do not compile platform-specific code

## Current Architecture Liabilities

Following GEA's guidance on architectural health:

### 1. `Engine` is Too Broad (GEA §1.6 — "beware the monolithic engine class")
The `Engine` class in `src/engine/` still owns rendering, physics, networking, UI, gameplay,
and debugging. Wrapping it behind `GameRuntime` is a start, but the internal responsibilities
must be decomposed into the subsystem layers described above.

### 2. No Unified Resource Manager (GEA §7.2)
Assets are loaded ad-hoc by individual subsystems. There is no GUID registry, no lifetime
management, and no cross-reference resolution. Adding a resource manager is a prerequisite
for data-driven content pipelines.

### 3. No Game Object Model (GEA §16.2)
Gameplay entities are managed implicitly through system-specific containers
(`PlayerControllerSystem`, `VoxelCollisionWorld`). A component-based object model would
enable data-driven entity definitions, event-driven communication, and scriptable behavior.

### 4. Renderer is Monolithic (GEA §11.2)
The renderer lacks the layered structure described in GEA: there is no material system,
no effect framework, no scene graph abstraction, and no separation between low-level
device interface and high-level draw submission.

### 5. Android/Web Bypass Shared Runtime (GEA §1.6.15)
Android and Web entry points do not yet use the `GameRuntime` path that desktop uses.
This means gameplay, networking, and menu behavior can drift across targets.

## Platform Runtime Matrix

| Target | Entry Point | Runtime Model | Status |
|---|---|---|---|
| **Desktop** | `src/game/main.cpp` | `GameRuntime` + platform adapters | Primary |
| **Dedicated Server** | `src/game/server_main.cpp` | `NetServer` standalone, no renderer | Stable |
| **Android** | `src/game/android_main.cpp` | Native activity, separate loop | Active |
| **Web** | `src/game/web_main.cpp` | Emscripten, shared session helpers | Preview |
| **iOS** | `src/platform/ios_platform.cpp` | Scaffold only | Scaffold |
| **Console** | `src/platform/console_platform.cpp` | Scaffold only | Scaffold |
| **XR** | `src/engine_xr/` | Scaffold only | Scaffold |

## Build Graph

Following GEA's recommended build layering (Chapter 6):

| Tier | Modules | Dependency Rule |
|---|---|---|
| **1. Foundation** | `engine_core`, `engine_math` | No engine dependencies |
| **2. Domain** | `engine_world`, `engine_physics`, `engine_net_proto`, `engine_server` | Depends on Tier 1 |
| **3. Network** | `engine_net` | Depends on Tier 2 (protocol) |
| **4. Gameplay/Assets** | `engine_gameplay`, `engine_assets`, `engine_audio`, `engine_ui` | Depends on Tiers 1-3 |
| **5. Orchestration** | `engine_runtime`, `engine_presentation` | Depends on Tiers 1-4 |
| **6. Render** | `engine_render` | Depends on Tier 1 |
| **7. Platform/Apps** | `platform`, `game` | Depends on Tiers 1-6 |

## Design Principles (from GEA)

1. **Data-driven over hard-coded**: Define behavior in data, not code. Use external assets,
   configuration files, and scripting where practical.
2. **Composition over inheritance**: Game objects compose components rather than inheriting
   from deep class hierarchies.
3. **Bulk updates over per-object iteration**: Subsystems update all instances of a component
   type at once for cache coherence.
4. **Fixed-step simulation**: Physics and gameplay update at a fixed rate independent of
   render frame rate.
5. **Explicit startup/shutdown**: `startUp()` and `shutDown()` methods called in explicit
   order from `main()`, never relying on global constructor ordering.
6. **Hashed string IDs**: Use 32/64-bit integer identifiers instead of raw strings for
   runtime comparisons.
7. **RAII janitors**: Bind resource acquisition/release to constructor/destructor scope
   guards (allocators, mutex locks, file handles).
8. **Middleware-first**: Use proven third-party libraries (Jolt, ENet, miniaudio, cgltf)
   rather than building custom solutions for well-solved problems.
9. **KISS (Keep It Simple, Stupid)**: Every data-driven feature must justify its build cost.
   Prefer simple solutions that work over complex ones that might work later.
