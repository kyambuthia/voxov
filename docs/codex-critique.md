# Ruthless Critique of `docs/roadmap.md`

The plan has the right broad destination, but it is not an execution plan. It is a pile of architectural nouns arranged into seven phases. It underestimates the platform migration, treats rendering as a mechanical API swap, defers validation until the end, and pretends "GEA layering" can be achieved by moving files into folders. The current codebase is not just GLFW/OpenGL with a few rough edges; `Engine` owns gameplay, networking, prediction, sessions, physics, vehicles, minigames, assets, renderer state, HUD, and debug state. The roadmap does not confront that dependency knot early enough.

## 1. Critical Areas Missing or Under-Planned

### No current-state inventory

Phase 1 starts by adding sokol headers, but it never requires a hard inventory of:

- every GLFW call site
- every raw OpenGL call site
- every ImGui backend dependency
- every miniaudio include and runtime assumption
- every public target that leaks `glfw`, `OpenGL::GL`, or GL headers
- every platform entrypoint: desktop, Android, Web, server, iOS scaffold, console scaffold
- every shader, vertex layout, texture format, render target, and GPU state assumption

Without this inventory, the migration will be driven by compiler errors. That is the slowest and least reliable way to do it.

### No renderer capability contract

The plan says "create `IRenderBackend` implementation using sokol_gfx" but does not define the backend contract. That is a fatal omission. Before touching sokol, the engine needs a rendering capability matrix:

- buffer update model: immutable, dynamic, streaming, orphaning behavior
- texture formats, mip generation, compression, sRGB behavior
- depth formats and compare modes
- render pass/load/store semantics
- offscreen render target ownership
- shader input layout rules
- uniform block layout and alignment
- instancing support
- index size support
- coordinate conventions and clip-space differences
- MSAA support
- backend limitations for GL, GLES/WebGL, Metal, D3D11, and WebGPU if relevant

Sokol is not "OpenGL with cleaner names." It forces a different resource lifetime and pipeline-state model. The plan does not specify how Voxov's existing `RenderScene`, `RenderMesh`, debug meshes, cached meshes, and ImGui rendering map into that model.

### Shader migration is barely mentioned

"Port shader management" is not a plan. Existing GLSL code and inline shader strings need a shader strategy:

- decide whether to use `sokol-shdc`
- define shader source language and target backends
- generate reflection metadata for attributes, uniforms, textures, samplers
- enforce uniform block layout
- integrate generated shader files into CMake
- add shader rebuild dependencies
- decide how debug and development builds report shader errors

This should be a Phase 1 concern. If shader translation is left until Phase 4, the renderer migration will stall immediately.

### Asset/resource work is too vague

The resource manager phase lists reference counting, async loading, hot reload, bundles, cooked glTF, texture compression, collision mesh generation, and migration of all asset loads in two weeks. That is not scoped. It is multiple systems:

- resource identity and stable names
- import pipeline
- cooked runtime format
- dependency graph
- versioning and invalidation
- asset database/catalog
- file watching
- async IO
- lifetime policy
- GPU upload scheduling
- hot reload safety
- tooling UX
- failure placeholders
- platform packaging

The roadmap does not say how resources move across threads, when GPU resources may be created, who owns CPU-side source data, or how live reload interacts with active animation/render/physics state.

### No explicit frame pipeline

GEA's architecture is not just a directory layout. A real engine needs a predictable frame:

1. platform events
2. input sampling
3. network receive
4. fixed simulation ticks
5. physics
6. animation
7. gameplay
8. replication
9. presentation extraction
10. render submission
11. audio update
12. profiling/debug presentation

The roadmap does not define this. Phase 5 says `tick(float dt)` for every subsystem, which is too weak. Networking prediction, physics, animation, and rendering do not all belong in the same variable-delta tick bucket.

### No threading model

The plan mentions jobs and async resource loading but never defines:

- which thread owns sokol_app callbacks
- which thread owns `sg_*` calls
- whether resource loading can create GPU objects directly
- how jobs synchronize with the render frame
- how hot reload avoids replacing resources currently in use
- whether audio callbacks can touch engine state
- how network IO and server simulation threads interact with gameplay state

This matters immediately. Sokol APIs generally assume a main-thread/event-callback driven app model. Treating sokol as a GLFW drop-in will produce lifecycle and threading bugs.

### No testing gates per phase

The plan defers serious testing to Phase 7. That is backwards. Every phase needs exit criteria:

- build matrix passes
- smoke app passes
- existing gameplay still launches
- renderer visual parity captured by screenshots
- network tests pass
- memory leak checks pass
- perf baseline does not regress beyond a threshold
- Android/Web targets either pass or are explicitly out of scope

"Polish, Testing, and Documentation" at the end is how migrations fail late.

### No rollback or compatibility strategy

The plan says GLFW/raw OpenGL are gone from the main build in Phase 1. That is reckless. Keep the old renderer/platform backend until sokol reaches parity. Use compile-time backend selection and run both paths against the same render scenes during migration. Deleting the old path before parity removes the only known-good reference.

### No observability plan

GEA treats profiling/debug as a first-class engine layer. The roadmap puts meaningful profiling in Phase 7. That is wrong. The migration needs:

- CPU frame timeline
- GPU timing where available
- resource lifetime counters
- per-subsystem memory counters
- draw call, buffer, texture, shader, pipeline counters
- network packet/latency/jitter metrics
- async load queue metrics
- platform event tracing

Without this, every performance and lifecycle problem becomes anecdotal.

## 2. Risks Not Accounted For

### Sokol app lifecycle does not match the current desktop abstraction

The current `DesktopPlatform` owns a `GLFWwindow*`, exposes `poll_events()`, `should_close()`, `native_window()`, `now_seconds()`, fullscreen controls, and direct GLFW access. `sokol_app` owns the application entrypoint and drives callbacks. That is not a simple internal replacement. The roadmap does not address how the existing `GameRuntime`, desktop adapter, Android main, Web main, and server executable coexist with sokol's callback model.

### Public target leakage

CMake currently links `glfw` and `OpenGL::GL` through public targets: `platform_desktop`, `engine_render`, `imgui`, `voxov`, and indirectly through `game_runtime`. Removing GLFW/OpenGL is not one line in CMake. The target graph must be redesigned so platform and render dependencies do not leak upward.

### ImGui migration is non-trivial

The roadmap says "Add `sokol_imgui.h`." Current ImGui integration uses `imgui_impl_glfw.cpp` and `imgui_impl_opengl3.cpp`. Switching to sokol means changing input event routing, frame begin/end sequencing, font texture creation, DPI handling, clipboard/text input behavior, and render pass integration.

### Audio replacement is probably wrong

Replacing miniaudio with `sokol_audio.h` may be a downgrade. Sokol audio is a low-level stream callback facility, not a full audio engine. The roadmap claims "stream management, sample playback, spatial audio stubs" but does not plan a mixer, decoding, resampling, channel management, device changes, underrun handling, or callback-safe command queues. If Voxov needs only UI beeps, keep it tiny. If it needs real game audio, `sokol_audio` alone is insufficient.

### "sokol_gfx compute" is bogus

Phase 4 proposes "GPU-driven, `sokol_gfx` compute" particles. `sokol_gfx` is a graphics abstraction, not a general compute API. Building a compute-based particle system on it is not a valid plan. Either make particles CPU/instancing based, or choose a graphics backend abstraction that actually exposes compute.

### Web and Android are treated as checkboxes

The repo already has Android and Web entrypoints. The roadmap says platform verification happens in Phase 7 and old platform backends get archived. That risks breaking the existing mobile/web scaffolding for most of the migration. WebGL constraints, Android lifecycle, touch input, asset packaging, audio unlock behavior, and filesystem differences must influence the sokol design from Phase 1.

### Multiplayer depends on earlier architecture but is scheduled after massive churn

Phase 6 assumes prediction/reconciliation/interpolation can be "ported" after the engine is refactored. In reality, prediction architecture constrains:

- fixed tick ownership
- input capture timestamps
- physics determinism expectations
- snapshot serialization
- presentation interpolation
- entity/component identity
- resource availability for replicated entities

Deferring multiplayer architecture until after renderer/resource/subsystem rewrites risks redoing the same boundaries again.

### Hot reload can corrupt live state

The plan says hot reload "works in dev mode" but does not discuss safety. Reloading meshes, textures, skeletons, animations, physics collision, or materials while gameplay and rendering hold references requires generation handles, staging resources, dependency invalidation, fallback resources, and frame-delayed destruction.

### File deletion plan is unsafe

Phase 7 says delete `src/main.cpp`, `src/game.cpp`, `src/renderer/vulkan_app.*`, `src/world/planet_world.*`, and archive platform backends. Some of those may be legacy, but deletion should follow usage proofs, build target analysis, and parity checks. The roadmap assumes dead code from filenames.

## 3. Weakest Phase

Phase 1 is the weakest phase because it claims the highest-risk technical migration can be completed in two weeks with only a triangle/beep smoke test.

It combines all of this into one phase:

- replace windowing
- replace application lifecycle
- replace GPU abstraction
- replace audio backend
- change build graph
- remove GLFW from public headers
- remove OpenGL from the main build
- integrate ImGui
- preserve existing game runtime behavior

That is not a phase. That is the whole migration's critical path compressed into the first sprint.

The deliverable says "Voxov runs on sokol stack" and "GLFW and raw OpenGL are gone from the main build." A triangle test does not prove that. It proves only that sokol can open a window. The real deliverable should be narrower:

- sokol backend compiles beside the OpenGL backend
- the engine can launch through sokol on Linux
- one existing representative scene renders through the same `RenderScene` path
- input, resize, fullscreen, timing, and ImGui work
- old backend still exists as parity reference

Phase 7 is also weak, but in a different way: it treats testing, performance, docs, cleanup, and platform verification as a final cleanup bucket. That guarantees late surprises. Still, Phase 1 is the most structurally broken because it sets an impossible baseline for every later phase.

## 4. Unrealistic Timeline Assumptions

The whole 12-week timeline is fantasy for the stated scope unless the target quality is "barely compiles on Linux with reduced features."

Specific unrealistic assumptions:

- **Weeks 1-2:** Full GLFW/OpenGL/miniaudio replacement plus platform layer cleanup. This is more realistically 4-6 weeks if visual parity, input, ImGui, CMake cleanup, and at least Linux/Web/Android consideration are included.
- **Week 3:** Assertions, memory auditing, math utilities, job dependency chains, containers, and performance counters. That is not one week unless most of it already exists and only needs documentation.
- **Weeks 3-4:** Full resource manager, asset cooker expansion, cooked glTF, texture compression, collision mesh generation, bundles, async loading, hot reload, and migration of existing asset loads. This is 6-10 weeks by itself if done seriously.
- **Weeks 4-6:** Low-level renderer, scene graph, culling, render queues, particles, post-processing, shadows stubs, decals stubs, HUD, ImGui, menus, debug draw, and renderer cleanup. This is not two weeks. Also, particles/post-processing/shadows should not be in the same phase as backend stabilization.
- **Weeks 6-8:** Decompose the `Engine` God object after all prior churn. Current `Engine` is deeply entangled; clean extraction with tests and no regressions is easily several weeks.
- **Weeks 8-10:** Authoritative multiplayer with prediction, reconciliation, interpolation, session UI, dedicated server CLI, monitoring, and graceful shutdown. This is not two weeks, especially after entity/resource/system boundaries just changed.
- **Weeks 10-12:** Tests, CI, performance optimization, documentation, all-platform verification, and legacy cleanup. This should happen throughout, not as a two-week final pass.

The plan also overlaps Phase 2 and Phase 3 in Week 3, then overlaps Phase 3 and Phase 4 in Week 4. That is not inherently bad, but the dependencies are wrong: resource identity and render resource lifetime should be defined before the renderer is rewritten, not while it is being rewritten.

## 5. Technical Challenges That Will Be Harder Than the Plan Assumes

### Render state conversion

OpenGL allows ambient mutable global state. Sokol wants explicit pipelines, bindings, passes, images, buffers, and shader descriptors. Existing code with dirty flags, cached VAOs/VBOs, inline GLSL, and implicit GL state will not port mechanically.

### Dynamic mesh updates

The existing renderer updates debug/world meshes and caches uploaded meshes. Sokol buffer update rules and per-frame update constraints need a deliberate streaming strategy. Debug draw, voxel chunks, skinned players, HUD text, and transient meshes should not all use the same upload path.

### ImGui and render pass ordering

With sokol, ImGui must be submitted inside the correct pass with the correct viewport, DPI scale, scissor state, and resource lifetime. Split-screen and overlay rendering complicate this.

### Input semantics

Replacing `glfwGetKey()` is not enough. Input needs edge/level state, text input, mouse capture, controller mapping, touch mapping, focus loss behavior, pause/resume behavior, and deterministic per-frame sampling. Sokol events should feed an engine HID layer; gameplay should not query platform state.

### Timing and fixed update

The plan adds `sokol_time.h`, but that does not solve frame pacing, fixed simulation ticks, prediction history, interpolation delay, physics stepping, or server tick consistency. Multiplayer and physics require a precise time model.

### Resource handles

"Strongly-typed 64-bit resource handles" is not enough. Handles need generation counters or equivalent stale-handle protection. The manager needs thread-safe state transitions: unloaded, loading, loaded CPU, uploaded GPU, failed, reloading, tombstoned.

### Cooked asset format

Cooking glTF into a runtime format is difficult if the renderer/material/animation/resource contracts are not settled. Texture compression also depends on target platforms. Web, Android, desktop GL/Metal/D3D have different supported formats.

### Animation and networking identity

Moving `SkinnedModel`, animation state, replicated player motion, and remote interpolation into systems requires stable entity identity. The roadmap does not define ECS, component storage, handles, scene ownership, or how network IDs map to runtime entities.

### Dedicated server separation

`voxov_server` should not link renderer, audio, platform windowing, ImGui, or asset systems that are client-only. The plan does not explicitly enforce headless dependency boundaries.

### Build/platform matrix

Sokol backend defines differ by platform. The plan does not specify `SOKOL_GLCORE`, `SOKOL_GLES3`, `SOKOL_METAL`, `SOKOL_D3D11`, `SOKOL_WGPU`, or platform-specific compiler/linker behavior. It also does not say how sokol implementation translation units are isolated to avoid one-definition-rule problems.

## 6. What Phase 1 Should Include But Does Not

Phase 1 should start with design and containment, not dependency copying.

Required additions:

- Produce a migration inventory of GLFW/OpenGL/miniaudio/ImGui backend usage.
- Define the platform API contract: lifecycle, events, input, time, window state, DPI, clipboard/text input, fullscreen, file paths, suspend/resume.
- Define the render backend contract before implementing sokol.
- Define a shader strategy using `sokol-shdc` or a clearly documented alternative.
- Add a backend capability matrix for desktop, Web, Android, macOS/iOS, and Windows.
- Create a single sokol implementation translation unit and CMake target with platform backend defines.
- Keep OpenGL as a parallel backend until visual parity is proven.
- Create a render parity scene using real engine geometry, debug draw, text/HUD, and a skinned model, not just a triangle.
- Add screenshot or image-hash smoke tests where practical.
- Add build gates for at least Linux desktop and whichever existing target must remain alive next: Web or Android.
- Add lifecycle tests for resize, minimize/restore, fullscreen toggle, focus loss, and shutdown.
- Add input event translation tests for keys, mouse, text, and touch/controller if in scope.
- Add an audio decision record: keep miniaudio, replace with sokol_audio only for a tiny mixer, or build a proper engine audio layer above a low-level callback.
- Establish performance baselines before changing rendering.
- Define a frame loop skeleton that later systems will plug into.
- Add logging/error handling for sokol validation failures.
- Add explicit "not in Phase 1" exclusions so scope does not explode.

## 7. Deviations From GEA Best Practices

### Layering is treated as folders, not dependencies

GEA layering is about dependency direction and runtime ownership. The plan creates directories like `low_level`, `scene_graph`, and `frontend`, but does not specify target boundaries or include rules. Current CMake already has problematic upward dependencies, such as assets depending on gameplay/render and gameplay depending on render. Moving files will not fix that.

### Platform independence is not established before platform replacement

GEA would put a stable platform independence layer between OS/SDKs and engine systems. The roadmap replaces GLFW with sokol while simultaneously defining the abstraction. That invites sokol details to leak upward exactly like GLFW did.

### Resource management is too late for renderer work

GEA's resource manager sits below rendering and feeds it. The roadmap starts the renderer backend migration in Phase 1, then creates the resource manager in Phase 3, then formalizes rendering in Phase 4. That sequence causes duplicated resource lifetime decisions.

### Profiling/debug is delayed

GEA treats profiling/debug tooling as a core engine concern. The roadmap leaves meaningful performance work and CI to Phase 7. A migration of this size needs profiling and diagnostics from the start.

### Game-specific logic remains mixed with engine services too long

The roadmap waits until Phase 5 to split `Engine` and `GameRuntime`, after platform, resource, and renderer churn. GEA would clarify engine services versus game-specific layer earlier, because that boundary determines what belongs in platform, rendering, resources, physics, animation, HID, and networking.

### The subsystem interface is too naive

`ISubsystem::tick(float dt)` is not a GEA-quality subsystem architecture. It ignores:

- init dependencies
- shutdown order
- fixed vs variable updates
- pre/post phases
- thread ownership
- service discovery
- event/message routing
- memory ownership
- profiling scopes
- failure handling
- headless/client-only/server-only subsystem variants

This would create a different God object: a subsystem registry with vague lifecycle calls.

### "Multiplayer-first" is asserted, not designed

The architecture principles say multiplayer-first, but the actual plan places multiplayer in Phase 6. If single-player is truly multiplayer with one client, entity identity, deterministic simulation boundaries, input commands, snapshots, resource spawning, and authority rules must shape Phases 1-5.

### Tools are not first-class

GEA-style engines are tool-heavy. The roadmap mentions `voxov_asset_cooker`, hot reload, ADRs, and docs, but does not define editor/tool runtime boundaries, asset database ownership, cooked-vs-source asset workflows, or how tools use the same resource/render/platform services without launching the full game.

### Data-driven gameplay is not planned

The principles mention data-driven behavior, but no phase introduces data schemas, config loading, entity archetypes, component definitions, scripting, tuning tables, or validation. The current roadmap mostly hardcodes system extraction.

## Bottom Line

The roadmap needs to be re-cut around risk, not architecture chapter headings.

A credible first version should look more like this:

1. Audit dependencies and define platform/render/resource contracts.
2. Build sokol beside the existing backend, with real-scene parity tests.
3. Establish frame pipeline, input/HID, timing, diagnostics, and build matrix.
4. Introduce resource handles and GPU resource lifetime before renderer expansion.
5. Split `Engine` along actual ownership boundaries before adding new renderer/resource/network features.
6. Preserve multiplayer constraints throughout, especially fixed tick, entity identity, and snapshot state.
7. Add platform verification, tests, profiling, and documentation continuously.

The current plan is ambitious but structurally brittle. It will produce a half-ported renderer, broken platform targets, delayed tests, and a second round of refactoring when multiplayer and resources expose the missing contracts.
