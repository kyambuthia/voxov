# Voxov Engine Rewrite Roadmap

> Target: Migrate from GLFW/OpenGL to **sokol**, restructure codebase per *Game Engine Architecture* (3rd Ed., Jason Gregory).
>
> GEA Runtime Engine Architecture Layers: Target Hardware → Device Drivers → OS → Third-Party SDKs → **Platform Independence** → Core Systems → Resource Manager → Rendering → Profiling/Debug → Collision/Physics → Animation → HID → Audio → Networking → Gameplay Foundations → Game-Specific

---

## Phase 1: Platform Independence Layer + Sokol (Weeks 1–2)

**Goal:** Replace GLFW + OpenGL + miniaudio with sokol. Build the GEA "Platform Independence Layer" so nothing above touches platform specifics.

### 1.1 Add sokol dependencies
- Add sokol single-header libraries to `dependencies/sokol/`:
  - `sokol_app.h` — cross-platform windowing (replaces GLFW)
  - `sokol_gfx.h` — cross-platform GPU API (replaces raw OpenGL)
  - `sokol_glue.h` — glue between sokol_app and sokol_gfx
  - `sokol_time.h` — high-resolution timing
  - `sokol_audio.h` — cross-platform audio (replaces miniaudio)
  - `sokol_log.h` — logging integration
- Add sokol_imgui.h for Dear ImGui integration
- Add sokol_gl.h if any legacy GL code needs porting shim

### 1.2 Create Sokol platform backend
- Create `src/platform/sokol/sokol_platform.hpp/.cpp`
  - Wrap sokol_app window creation, event polling, swap chain
  - Implement `app_init`, `app_frame`, `app_cleanup`, `app_event` callbacks
  - Remove `GLFWwindow*` from public headers
- Update `PlatformCreateInfo` to remove GLFW-specific fields
- Make `DesktopPlatform` use sokol_app internally
- Delete `glfwGetKey()` direct calls in `src/game/main.cpp`

### 1.3 Create Sokol render backend
- Create `src/engine_render/sokol_renderer.hpp/.cpp`
  - New `IRenderBackend` implementation using sokol_gfx
  - Port: mesh upload, shader management, draw calls, render targets
  - Add sokol_gfx backend enum to `RenderBackendType`
- Move `gl_renderer.cpp/.hpp` to legacy (`VOXOV_BUILD_LEGACY_DEMOS`)
- Wire sokol renderer into `Renderer::init()` via backend selection

### 1.4 Replace audio backend
- Create `src/engine_audio/sokol_audio_backend.cpp`
- Replace miniaudio calls with sokol_audio.h
- Port: stream management, sample playback, spatial audio stubs

### 1.5 Update build system
- Remove `find_package(OpenGL)` and GLFW from CMakeLists
- Add sokol compilation flags (`SOKOL_IMPL`, backend defines)
- Keep GLM, Jolt, ENet, ImGui, cgltf, fmt, spdlog — they stay
- Wire new sokol source files into build targets

### 1.6 Smoke test
- Write minimal triangle + audio beep test (`src/tests/sokol_smoke.cpp`)
- Verify: window opens, triangle renders, audio plays — on Linux desktop
- Build target: `voxov_sokol_smoke`

**Phase 1 Deliverable:** Voxov runs on sokol stack. GLFW and raw OpenGL are gone from the main build. Platform layer has clean abstraction.

---

## Phase 2: Core Systems Formalization (Week 3)

**Goal:** Solidify `engine_core` to match GEA section 1.6.6 standards.

### 2.1 Assertion system
- Add `src/engine_core/assert.hpp`
  - `VOXOV_ASSERT(expr)` — debug-build assertion with message, file, line
  - `VOXOV_ASSERT_MSG(expr, msg)` — assertion with format string
  - `VOXOV_VERIFY(expr)` — assertion that stays in release builds
  - `VOXOV_FATAL(msg)` — unconditional fatal error
- Gated by `VOXOV_ENABLE_ASSERTIONS` (already in CMake)

### 2.2 Memory system audit
- Review `src/engine_core/memory.hpp/.cpp`
  - Document allocator interface
  - Add tagged allocation support for leak tracking
  - Add debug-mode memory fences to catch buffer overruns
  - Add optional high-water-mark tracking per subsystem

### 2.3 Math library audit
- Review `src/engine_math/` — GLM is solid, document usage
- Add missing GEA math utilities:
  - Intersection tests (ray-sphere, ray-AABB, sphere-frustum)
  - Easing functions for animation curves
  - Random number generation utilities
- Consider adding a thin wrapper around GLM types for engine-specific semantics

### 2.4 Job system documentation
- Document `src/engine_core/jobs.hpp` API
- Add priority levels, dependency chains
- Add job system performance counters

### 2.5 Core data structures
- Add `src/engine_core/containers.hpp` with:
  - Fixed-size ring buffer
  - Intrusive linked list (for low-allocation entity management)
  - Handle table (stable IDs for entity references)
  - Object pool (contiguous storage, O(1) alloc/free)

**Phase 2 Deliverable:** Core systems are documented, robust, and cover all GEA core services.

---

## Phase 3: Resource Manager (Weeks 3–4)

**Goal:** Build proper asset pipeline per GEA section 1.6.7.

### 3.1 Resource base types
- Create `src/engine_resources/` library
  - `resource_id.hpp` — strongly-typed 64-bit resource handles
  - `resource.hpp` — base class for all loadable resources
  - `resource_loader.hpp` — async loading interface

### 3.2 Resource manager core
- Create `src/engine_resources/resource_manager.hpp/.cpp`
  - Central registry of all loaded resources
  - Reference counting with automatic unload
  - Async load requests with callbacks
  - Synchronous "block until loaded" fallback
  - Hot-reload support (watch files, reload on change)

### 3.3 Asset pipeline
- Extend `voxov_asset_cooker` to:
  - Compile glTF → engine binary format
  - Cook textures (mipmaps, compression)
  - Generate collision meshes from render meshes
  - Package assets into bundles (PAK-like archives)
- Create `src/tools/` for offline tools

### 3.4 Migrate existing asset loading
- Move `SkinnedModel` loading through resource manager
- Move font loading through resource manager
- Remove ad-hoc `try_load_character_model()` from Engine class

**Phase 3 Deliverable:** All assets loaded through resource manager. Hot-reload works in dev mode.

---

## Phase 4: Rendering Engine Layering (Weeks 4–6)

**Goal:** Structure the renderer per GEA section 1.6.8 — Low-Level Renderer, Scene Graph/Culling, Visual Effects, Front End.

### 4.1 Low-level renderer formalization
- `src/engine_render/low_level/`
  - `graphics_device.hpp` — device init, swapchain, backbuffer management
  - `render_context.hpp` — per-frame render state, command lists
  - `shader.hpp` — shader compilation and caching (sokol sg_shader)
  - `material.hpp` — material = shader + uniforms + textures + render state
  - `mesh.hpp` — GPU buffer management (vertex + index buffers)
  - `texture.hpp` — texture creation, upload, sampling

### 4.2 Scene graph / culling
- Create `src/engine_render/scene_graph/`
  - Spatial subdivision data structure (octree for voxel world)
  - Frustum culling pass
  - Occlusion culling stubs (portal-based for later)
  - Render queue generation (opaque → transparent → overlay)

### 4.3 Visual effects
- `src/engine_render/effects/`
  - Particle system (GPU-driven, sokol_gfx compute)
  - Post-processing pipeline (HDR, bloom, color grading)
  - Shadow mapping stubs
  - Decal system stubs

### 4.4 Front end
- `src/engine_render/frontend/`
  - HUD rendering (text, bars, minimap)
  - ImGui integration via sokol_imgui.h (dev UI, console)
  - In-game menu system
  - Debug drawing (lines, boxes, text — port existing `debug_draw/`)

### 4.5 Clean up existing renderer
- Port `render_types.hpp` into low-level types
- Remove dead Vulkan code (`src/renderer/vulkan_app.*`)
- Consolidate `RenderScene` → low-level submission API
- Update `RenderFrameContext` for sokol_gfx

**Phase 4 Deliverable:** Renderer has proper layered architecture. Scene graph performs culling. Effects system has particle and post-processing pipelines.

---

## Phase 5: Decompose the God Object (Weeks 6–8)

**Goal:** Pull all subsystems out of `Engine` class into proper GEA subsystem interfaces.

### 5.1 Define subsystem interface
- Create `src/engine/subsystem.hpp`
  ```cpp
  class ISubsystem {
  public:
      virtual ~ISubsystem() = default;
      virtual const char* name() const = 0;
      virtual bool init(const SubsystemConfig& config) = 0;
      virtual void shutdown() = 0;
      virtual void tick(float dt) = 0;
  };
  ```

### 5.2 Extract subsystems
- `PhysicsSystem` — already mostly standalone in `engine_physics/`
  - Extract from Engine: collision world init, physics step, vehicle sim
  - Interface: `step(dt)`, `query_world()`, `add/remove_body()`
- `AnimationSystem` — from `engine_gameplay/animation/`
  - Extract: animation state machines, skeletal animation, blending
  - Interface: `update_animations(dt)`, `get_bone_matrices(entity)`
- `AudioSystem` — from `engine_audio/`
  - Extract: stream management, spatial audio, UI sounds
  - Interface: `play_sound(id, pos)`, `set_listener(pos, orient)`
- `HIDSystem` — from `engine_input/`
  - Extract: input polling, action mapping, controller support
  - Interface: `poll_input()`, `get_action("jump")`, `get_axis("move")`
- `NetworkingSystem` — from `engine_net/`
  - Extract: NetClient, NetServer, LAN discovery, replication
  - Interface: `connect(host, port)`, `send_reliable(msg)`, `get_snapshot()`
- `GameplaySystem` — from scattered Engine code
  - Extract: player spawning, vehicle management, minigames, objectives
  - Interface: `spawn_player()`, `get_player(id)`, `get_vehicle(id)`
- `DebugSystem` — from debug drawing and profiling
  - Extract: debug draw, profiler display, console commands, cvars
  - Interface: `draw_line(a, b, color)`, `add_profiler_scope(name)`

### 5.3 Rewrite Engine as orchestrator
- `Engine` becomes a thin orchestrator:
  - Owns all subsystems
  - Manages init/shutdown order via dependency graph
  - Routes tick calls
  - Owns the game loop timing
- Remove all simulation state from Engine (vehicles, players, minigames)
- Engine holds only: subsystem registry, frame pacing, session state

### 5.4 Update game_runtime
- `GameRuntime` (already a pimpl wrapper) consumes Engine services
- Clear separation: Engine = services, GameRuntime = game logic
- `src/game/` becomes the "game-specific" layer from GEA Figure 1.31

**Phase 5 Deliverable:** Engine class is ~200 lines. All subsystems are independently testable. Clear engine/game boundary.

---

## Phase 6: Multiplayer Foundation (Weeks 8–10)

**Goal:** GEA section 1.6.14 — authoritative server, client prediction, entity interpolation.

### 6.1 Server authority model
- Voxov server is the authority for all game state
- Clients send inputs, server simulates, sends back state snapshots
- Server validates all client actions (anti-cheat foundation)

### 6.2 Network replication
- Create `src/engine_net/replication/`
  - `replicated_object.hpp` — base for network-synced entities
  - `replication_manager.hpp` — manages replication sets
  - Property replication: create, update, destroy
  - Interest management: only replicate entities near player
  - Delta compression: only send changed properties

### 6.3 Client-side prediction
- Client predicts own movement locally
- Server sends authoritative state
- Reconciliation: correct client when prediction differs
- Visual smoothing for corrections
- Port existing `ReconcileMode` and `PredictionHistoryEntry` into proper system

### 6.4 Entity interpolation
- Remote entities rendered at interpolated positions
- Buffer of past snapshots, render slightly behind server time
- Smooth visual motion despite network jitter
- Port existing `RemoteRenderPlayer` interpolation logic

### 6.5 Session management
- Create/host session UI flow
- Join via IP or LAN discovery
- Session state: lobby → playing → ended
- Player join/leave events
- Session migration (host transfer) stubs for later

### 6.6 Server headless mode
- `voxov_server` runs as dedicated server (already exists)
- CLI arguments: `--port`, `--max-players`, `--map`
- Server logging and monitoring
- Graceful shutdown with player notification

**Phase 6 Deliverable:** Client-server multiplayer works end-to-end. Client prediction + server reconciliation produces smooth local movement. Remote players appear with interpolation.

---

## Phase 7: Polish, Testing, and Documentation (Weeks 10–12)

### 7.1 Test infrastructure
- Expand test coverage:
  - Unit tests per subsystem (math, containers, networking serialization)
  - Integration tests (physics + collision, renderer + scene graph)
  - Network stress tests (multiple clients, packet loss simulation)
- CI/CD pipeline: build + test on every commit

### 7.2 Performance optimization
- Profile with sokol's built-in instrumentation
- Optimize hot paths:
  - Voxel mesh generation (current bottleneck?)
  - Render batching (instanced rendering)
  - Network serialization (binary protocols, not text)
- Target: stable 120+ FPS on 1080p, 60+ FPS on integrated GPU

### 7.3 Documentation
- Architecture decision records (ADRs) in `docs/adr/`
- API documentation for each subsystem
- CONTRIBUTING.md for new developers
- Diagrams: engine layer stack, data flow, network model

### 7.4 Platform verification
- Test on all sokol-supported platforms:
  - Linux (primary) ✓
  - Windows (via MinGW or MSVC)
  - macOS
  - Web (Emscripten + WebGL)
  - Android (existing NDK support)
  - iOS (scaffold exists)

### 7.5 Cleanup
- Remove all legacy code behind `VOXOV_BUILD_LEGACY_DEMOS`
- Delete `src/main.cpp`, `src/game.cpp`, `src/renderer/vulkan_app.*`, `src/world/planet_world.*`
- Remove GLFW, raw OpenGL, miniaudio from dependencies
- Archive old platform backends (web, android, console, ios) — they get rebuilt on sokol

**Phase 7 Deliverable:** Stable, tested, documented engine. Portable across all sokol platforms. Clean codebase with no legacy artifacts.

---

## Architecture Principles (from GEA)

- **Layered architecture**: higher layers depend on lower, never circular
- **Platform independence**: all platform code behind abstraction
- **Resource management**: centralized, reference-counted, hot-reloadable
- **Data-driven**: behavior defined in data, not hardcoded
- **Component-based**: entities composed of subsystems, not monolithic classes
- **Multiplayer-first**: single-player is multiplayer with one client
- **Design for tools**: engine serves both runtime and editor tooling

## Dependencies (Post-Sokol Migration)

**Staying:**
- GLM — math library
- Jolt Physics — collision and rigid body dynamics
- ENet — UDP networking layer
- Dear ImGui — debug/development UI
- cgltf — glTF model loading
- fmt + spdlog — formatting and logging

**Replaced:**
- GLFW → sokol_app
- OpenGL / GLES → sokol_gfx
- miniaudio → sokol_audio

**Removed:**
- Vulkan stubs (dead code in `src/renderer/vulkan_app.*`)
- SDL references (was never fully integrated)
- Raw GL calls in platform code
