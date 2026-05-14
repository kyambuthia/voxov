# VOXOV Roadmap

This roadmap follows the layered subsystem order from Jason Gregory's *Game Engine
Architecture*. Each phase builds on the foundation below it. Phases are designed to
be executed in order — attempting later phases without completing earlier ones will
produce an unstable codebase.

## Guiding Principles

- **Correctness before features**: Each phase must produce a stable, testable baseline
  before moving on.
- **Small vertical slices**: Ship a working increment at each step, not a grand rewrite.
- **Reduce, then expand**: Shrink the monolithic `Engine` class before adding new systems.
- **Middleware-first**: Use proven libraries (Jolt, ENet, miniaudio, Dear ImGui) rather
  than building custom solutions for commoditized problems.

---

## Phase 1: Foundation Hardening (GEA §6 — Engine Support Systems)

**Goal**: Establish the core infrastructure that all other systems depend on.

### 1.1 Memory Management
- Implement custom allocators:
  - **Stack allocator**: For level loads and per-frame temporary data. O(1) allocation,
    free-to-marker, no fragmentation.
  - **Pool allocator**: For fixed-size objects (matrix palettes, mesh instances, events).
    O(1) allocation via free list, zero fragmentation.
  - **Single-frame allocator**: Cleared at the top of each frame. Blazingly fast.
  - **Double-buffered allocator**: For async processing results that span frame boundaries.
- Replace ad-hoc `new`/`malloc` calls with appropriate custom allocators.
- Keep heap allocations out of tight loops (GEA golden rule).

### 1.2 Hashed String IDs
- Implement compile-time string hashing (`operator"" _sid` in C++11).
- Replace raw string comparisons (`strcmp`, `strcpy`) with integer comparisons.
- Intern strings once at static initialization; never call `internString()` in a tight loop.
- Store human-readable string table only in debug builds.

### 1.3 Engine Configuration
- Implement console variables (cvars) system:
  - Global linked list or hash table of typed variables.
  - Command-line overrides (`--cvar value`).
  - Configuration file loading (JSON).
  - In-game console for inspection and modification (via Dear ImGui).

### 1.4 Subsystem Startup/Shutdown
- Formalize `startUp()` / `shutDown()` methods for all subsystem managers.
- Call in explicit dependency order from `main()` (startup: bottom-up; shutdown: top-down).
- Never rely on global constructor ordering.

### 1.5 Refactor `Engine` Class
- Extract responsibilities from `Engine` into focused subsystem managers:
  - `RenderManager` — rendering pipeline orchestration
  - `PhysicsManager` — physics world lifecycle and stepping
  - `NetworkManager` — client/server lifecycle
  - `InputManager` — input capture and action mapping
  - `AudioManager` — audio engine lifecycle
- `Engine` becomes a thin coordinator that calls each subsystem at the right time.

**Success criteria**: All existing tests pass. Minimal functional change. Subsystem startup
order is explicit and documented. No `new`/`malloc` in tight loops.

---

## Phase 2: Resource Manager (GEA §7 — Resources and the File System)

**Goal**: Build a unified asset management system that handles loading, lifetime, and
cross-references.

### 2.1 File System Abstraction
- Wrap OS file I/O behind a consistent `FileSystem` interface.
- Path manipulation API (extract, join, canonicalize, resolve relative/absolute).
- Platform-specific backends (POSIX, Win32, Android assets, Emscripten virtual FS).

### 2.2 Resource Manager
- GUID-keyed resource registry (single copy of each unique resource in memory).
- Reference-counted lifetime management:
  - **Global resources**: Loaded at boot, never unloaded.
  - **Level resources**: Loaded/unloaded with world chunks.
  - **Temporary resources**: Freed when refcount reaches zero.
- Composite resource loading (model = mesh + materials + textures + skeleton + animations).
- Cross-reference resolution (GUID-based lookup table).
- Post-load initialization hooks (upload mesh to GPU, compute tangents, etc.).

### 2.3 Asset Conditioning Pipeline
- Formalize the three-stage pipeline:
  1. **Exporter** (DCC → intermediate format: JSON or custom binary)
  2. **Resource compiler** (strip editor-only data, compress, platform-specific packing)
  3. **Resource linker** (combine sub-resources into final package)
- Extend `asset_cooker` tool to handle all asset types.
- Resource versioning for cache invalidation.

**Success criteria**: All assets loaded through the resource manager. No direct file I/O
in subsystem code. Asset lifetime correctly managed across level loads.

---

## Phase 3: Game Loop & Time (GEA §8 — The Game Loop and Real-Time Simulation)

**Goal**: Implement proper fixed-step game loop with abstract timelines.

### 3.1 Fixed-Step Simulation
- Decouple simulation tick rate from render frame rate.
- Fixed time step for physics (60 Hz) and gameplay (30 Hz or configurable).
- Accumulator pattern: track real time delta, consume in fixed steps, carry remainder.

### 3.2 Abstract Timelines
- **Real time**: High-resolution timer (CPU cycle counter or `clock_gettime`).
- **Game time**: Independent of real time. Supports pause (stop advancing), slow-motion
  (advance slower than real time), and single-step (advance one tick per button press).
- **Local timelines**: Per-clip time for animation and audio.
- Breakpoint handling: clamp measured delta to target frame interval if > 1 second.

### 3.3 Server Tick Scheduling
- Server simulates at a fixed rate independent of packet arrival.
- Consumes "latest input for each player" each tick.
- Emits snapshots at a controlled send rate (e.g., 20-30 Hz).
- Track per-player last-processed input tick; discard stale inputs.

### 3.4 Multiprocessor Game Loop
- Implement a job system for parallel subsystem updates:
  - **Fiber-based coroutine jobs** (Naughty Dog model): cooperative context switching
    via `SwitchToFiber()`. Fibers can yield/sleep without blocking the worker thread.
  - **Job counters** for dependency tracking: `KickJob(declaration)`, `WaitForCounter(counter)`.
  - **Spin locks** for low-contention synchronization; custom job mutex that yields the
    fiber (not the thread) for high-contention cases.

**Success criteria**: Game runs stable at target frame rate. Simulation is deterministic
given the same inputs. Server sends at controlled rate. Pause and slow-motion work.

---

## Phase 4: Rendering Engine Overhaul (GEA §11 — The Rendering Engine)

**Goal**: Restructure the renderer into GEA's layered architecture.

### 4.1 Low-Level Renderer
- **Graphics Device Interface (GDI)**: Abstract buffer creation, texture upload, shader
  compilation, and draw calls. Single OpenGL/GLES3 backend.
- **Material system**: Define reusable materials (wood, metal, skin). Each material =
  shader programs + textures + GPU state (blend mode, depth test, cull mode).
- **Effect files**: Organize shaders into techniques (quality tiers) and passes.
  Use GLSL with preprocessor-based variants (`#ifdef SKINNED`, `#ifdef NORMAL_MAP`).

### 4.2 Material & Shader System
- Material library: define materials in data files, referenced by mesh instances.
- Shader parameter binding: uniforms mapped by name, textures by sampler slot.
- Support for shader hot-reloading during development.

### 4.3 Scene Graph / Culling
- Implement octree spatial subdivision for the voxel world.
- Frustum culling: test bounding spheres against 6 frustum planes.
- Draw submission: iterate visible mesh-material pairs, set render state, call `glDrawElements`.

### 4.4 Render Pipeline Stages
1. **Z-prepass**: Render opaque geometry front-to-back, z-only (no fragment shader).
2. **Opaque pass**: Sort by material, full vertex + fragment shading.
3. **Sky pass**: Render sky dome/box.
4. **Transparent pass**: Sort back-to-front, alpha blending, depth-write off.
5. **Post effects**: Full-screen quad passes (bloom, tone mapping, gamma correction).
6. **HUD/overlays**: Screen-space quads with z-testing disabled.

### 4.5 Debug Rendering
- Debug line/sphere/box drawing (world-space, view-space).
- Wireframe overlay toggle.
- Collision geometry visualization.
- Skeleton visualization for animation debugging.

**Success criteria**: Renderer has clear layer boundaries. Materials are data-driven.
Frustum culling is active and measurable. Debug drawing is available.

---

## Phase 5: Animation System (GEA §12 — Animation Systems)

**Goal**: Implement a full animation pipeline with blend trees and state machines.

### 5.1 Animation Data Structures
- Formalize skeleton representation: array of joints, depth-first order, inverse bind pose
  cached per joint.
- Pose representation: SRT (Scale-Rotation-Translation) per joint — cleanly interpolatable.
- Global pose generation: hierarchy walk, local-to-global concatenation.
- Matrix palette generation for skinning: `K_j = (B_j→M)^(-1) × C_j→M`.

### 5.2 Animation Clips
- Load animation clips from glTF (via cgltf).
- Continuous timeline with floating-point time.
- Looping support (omit duplicate last frame).
- Playback rate control (R < 0 = reverse playback).

### 5.3 Blend Trees
- Atomic blend nodes: clip leaf, binary LERP, binary additive, ternary LERP.
- Generalized 1D blend space (clips along a linear parameter range).
- 2D blend space (cascaded binary LERP or Delaunay triangulation).
- Additive blending for variation on top of base motion.

### 5.4 Action State Machine (ASM)
- States contain blend trees. Transitions between states with crossfade durations.
- Layered ASM: multiple independent state machines per character (full-body, face, gestures).
- Named transition requests shield gameplay code from state graph structure.

### 5.5 Post-Processing
- Inverse Kinematics (IK): two-bone solver for foot placement on uneven terrain.
- Ragdoll driving: physics bodies determine joint transforms (enter ragdoll on death/knockback).
- Powered constraints: animation drives physics bodies for hybrid animation-physics.

### 5.6 Pipeline & Tooling
- Animation viewer with live reload of state definitions.
- Command-line recompilation of animation scripts without game restart.
- Offline compression (quantization, key omission, curve fitting).

**Success criteria**: Characters animate smoothly with blend transitions. Foot IK adapts
to terrain. Ragdoll enter/recover cycle works. Animation parameters are network-replicated.

---

## Phase 6: Collision & Physics Integration (GEA §13)

**Goal**: Fully integrate Jolt Physics for gameplay physics and collision queries.

### 6.1 Physics World Architecture
- Fixed-step physics update (60 Hz default, configurable).
- Shape library: sphere, capsule, AABB, convex hull, triangle mesh.
- Collision filtering: layer/mask tables for gameplay categorization.

### 6.2 Collision Detection
- Broad phase: sweep and prune on AABBs (Jolt built-in).
- Midphase: compound shape BVH traversal.
- Narrow phase: GJK/EPA for convex-convex (Jolt built-in).

### 6.3 Collision Queries
- Ray casts: line-of-sight checks, weapon traces.
- Shape casts: movement sweep tests.
- Phantoms: trigger volumes for gameplay zones.

### 6.4 Rigid Body Dynamics
- Rigid body creation from physics shapes.
- Force/torque/impulse application.
- Motion types: static, kinematic (animation-driven), dynamic (physics-driven).
- Sleep/wake management for performance.

### 6.5 Constraints
- Point-to-point, hinge, slider, prismatic constraints.
- Breakable constraints (force threshold).
- Powered constraints for animation-driven physics.

### 6.6 Ragdoll Physics
- Define ragdoll body rig (capsules for limbs, boxes for torso).
- Enter ragdoll: set pose from animation, apply impulse.
- Simulate and stabilize.
- Recover (get-up): drive ragdoll toward animated pose, then blend back.

**Success criteria**: Gameplay collision queries work reliably. Ragdolls simulate
plausibly and can recover. Physics runs at fixed step, decoupled from rendering.

---

## Phase 7: Audio Engine (GEA §14 — Audio)

**Goal**: Build a functional 3D audio system with sound banks and streaming.

### 7.1 Audio Engine Core
- Integrate miniaudio as the audio backend.
- Voice bus model: play → gain → filter → pan → mix.
- Multiple voice allocation with priority-based stealing.
- Virtual voice management: decouple logical playback from hardware voices.

### 7.2 3D Audio
- Distance attenuation with configurable fall-off (min/max radius).
- Stereo panning with constant-power pan law.
- Listener attached to camera.

### 7.3 Sound Banks
- Group audio clips into banks for loading/unloading as units.
- Core bank (UI sounds, footstep base) — always resident.
- Level banks — loaded/unloaded with world chunks.
- Streaming for music and long dialog (ring buffer).

### 7.4 Gameplay Integration
- Impact sounds driven by collision material types (physics → audio bridge).
- Footstep sounds synchronized to animation events (animation → audio bridge).
- UI sounds on menu navigation.
- Ambient and environmental sound zones.

**Success criteria**: 3D positional audio works. Sound banks load/unload with world.
Footstep and impact sounds play at correct times. Voice stealing avoids audio dropouts.

---

## Phase 8: Networking Hardening (GEA §1.6.14 — Online Multiplayer)

**Goal**: Strengthen the existing networking to production quality.

### 8.1 Protocol Hardening
- Delta compression for high-rate state updates.
- Interest management (only send relevant entities to each client).
- Bandwidth budgeting and rate limiting.
- NAT traversal for Internet play (STUN, relay fallback).

### 8.2 Server Authority
- Server-side collision/physics simulation (mirror client prediction logic).
- Anti-cheat validations (speed hacks, position teleport, impossible states).
- Graceful handling of client timeout and reconnection.

### 8.3 Transport Abstraction
- Define a `ITransport` interface behind ENet.
- Support multiple transport backends: ENet (native UDP), WebRTC (browser),
  WebTransport (future browser standard).
- Web target: implement WebRTC DataChannel transport via Emscripten.

**Success criteria**: Server-authoritative gameplay works. Delta compression reduces
bandwidth measurably. Transport abstraction allows swapping ENet for WebRTC.

---

## Phase 9: Gameplay Foundation (GEA §15-16 — Gameplay Systems)

**Goal**: Implement a component-based game object model with events and scripting.

### 9.1 Game Object Model
- Entity-component architecture:
  - **Entity**: Unique ID, name, transform.
  - **Component**: Attached to entity, owns specific concern.
  - **System**: Updates all components of a given type in bulk.
- Component types: `TransformComponent`, `MeshComponent`, `RigidBodyComponent`,
  `AnimationComponent`, `AudioComponent`, `ScriptComponent`.
- Entity creation from data (spawner/type schema pattern).

### 9.2 Event System
- Typed events with string/hashed-ID event types.
- Event parameters as variant collections or key-value pairs.
- Event queues with configurable delivery timing (immediate, deferred to phase boundary).
- Responsibility chain: events propagate along entity relationship graph (parent-child,
  team).

### 9.3 World Streaming
- Define load regions (convex volumes) around player interest points.
- Chunk-based streaming with asynchronous I/O.
- Priority-based loading (audio > critical textures > decorative).
- Seamless world traversal without loading screens.

### 9.4 Scripting Integration
- Embed Lua (or similar) for data-driven gameplay behavior.
- Scriptable event handlers: respond to game events without C++ recompilation.
- Script-driven finite state machines for AI and game flow.
- Hot-reloading of scripts during development.

### 9.5 High-Level Game Flow
- Finite state machine for game states: MainMenu → Loading → Playing → Paused → GameOver.
- Objective tracking and branching mission structure.
- Save/load system with versioning.

**Success criteria**: Game objects defined entirely in data. Events drive gameplay logic
without hard-coded call chains. World streams seamlessly. Scripts can be modified and
reloaded without restart.

---

## Phase 10: Polish & Cross-Platform Convergence

**Goal**: Unify all platform runtimes and reach production quality.

### 10.1 Runtime Convergence
- Migrate Android to `GameRuntime` path (thin adapter, shared simulation).
- Migrate Web to `GameRuntime` path (WASM-heavy runtime, JS for browser I/O only).
- Remove duplicate runtime paths after parity is established.

### 10.2 Platform Hardening
- Android: consistent 60 FPS on mid-range devices, proper lifecycle handling.
- Web: full-screen support, pointer lock, mobile browser compatibility.
- macOS: verify and fix rendering/input issues (if it remains an active target).

### 10.3 Verification
- CI matrix: Linux, Windows, macOS, Android emulator, Web (headless).
- Automated gameplay smoke tests (connect, move, interact, disconnect).
- Performance regression tracking (frame time, memory, network bandwidth).

### 10.4 Visual Polish
- Physically-based shading (PBR) material pipeline.
- Shadow mapping (cascaded shadow maps for directional light).
- Ambient occlusion (SSAO or pre-baked).
- Post effects: bloom, tone mapping, color grading.

---

## Deferred (Post-1.0)

These systems are intentionally deferred until the core architecture is stable:

- **Vehicles & Aircraft**: Reintroduce as first-class gameplay modes after runtime
  convergence (GEA §13 vehicle dynamics, §16.10 gameplay integration).
- **Advanced AI**: Pathfinding (A* on nav mesh), behavior trees, perception systems
  (GEA §17.2 gameplay systems).
- **Visual Effects**: Particle system overhaul, volumetric effects, advanced post
  processing (GEA §11.4).
- **Future Platforms**: XR, iOS, console — revisit only after shared runtime services
  are in place and Android/Web divergence is eliminated.
- **World Editor**: In-engine editing tools (UnrealEd model). Dependent on stable
  object model and data pipeline.

---

## Timeline Summary

| Phase | Content | Estimated Effort |
|---|---|---|
| 1 | Foundation: memory, strings, config, subsystem mgmt, Engine refactor | 3-4 weeks |
| 2 | Resource Manager + asset pipeline | 2-3 weeks |
| 3 | Game loop, time, job system | 2-3 weeks |
| 4 | Rendering engine overhaul | 4-6 weeks |
| 5 | Animation system | 3-4 weeks |
| 6 | Collision & physics integration | 2-3 weeks |
| 7 | Audio engine | 2-3 weeks |
| 8 | Networking hardening | 2-3 weeks |
| 9 | Gameplay foundation | 4-6 weeks |
| 10 | Polish & convergence | 2-4 weeks |

**Total estimated effort**: 26-40 weeks for a small team.

For context on execution priorities and practical implementation, see
`docs/VOXOV_IMPLEMENTATION_GUIDE.md`.
