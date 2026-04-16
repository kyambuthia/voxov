# Roadmap

This roadmap is based on the current repository shape. The main priority is to converge runtimes and reduce architectural drag before adding more platform surface area.

## Near Term

### 1. Deepen The Shared Runtime Core

- Move more desktop ownership out of `Engine` and behind `GameRuntime`:
  - session/game state
  - rendering scene assembly
  - multiplayer/session management
  - debug and tooling overlays
- Introduce platform services for:
  - save paths
  - asset roots
  - environment/runtime discovery
- Stop adding new feature ownership to the monolithic `Engine` where a focused subsystem would work.

### 2. Lock The Renderer To A Mobile-Friendly Budget

- Keep the shipping desktop renderer on an OpenGL-first path instead of rebuilding multi-backend scope.
- Treat GLES3/WebGL2-class constraints as the visual feature ceiling for shared content decisions.
- Spend freed-up engine time on frame pacing, streaming, tooling, and world quality instead of renderer diversification.

### 3. Add Basic Perf Telemetry First

- Add lightweight runtime perf telemetry before expanding other debug overlays.
- Start with:
  - frame time
  - fixed-step time
  - render CPU time
- network send/receive rates
- chunk streaming counters
- Keep the first pass cheap, always available in dev builds, and easy to surface in desktop, Android, and dedicated server builds.

### 4. Build Out The Visual Debug Stack

- After basic perf telemetry, add the rest of the gameplay/system visual debug tooling:
  - AI behavior debug
  - navigation and pathfinding debug
  - cover and combat debug
- adversary-state debug
- world-event and point-of-interest debug
- Keep these tools behind clear debug toggles and drive them from shared runtime services rather than platform-specific one-offs.

### 5. Reduce Platform Divergence

- Move Android toward shared simulation/session helpers instead of growing more custom runtime code.
- Move Web toward a WASM-heavy runtime where gameplay, menu/session flow, and simulation live in C++ and JavaScript is reduced to browser APIs, bootstrap, and transport glue.
- Keep iOS, console, and XR work in scaffold mode until desktop, Android, and Web are closer to one runtime model.

### 6. Harden The Dedicated Server Target

- Keep `voxov_server` independent from render and window-system requirements.
- Reduce reliance on the older `voxov --headless-server` compatibility path.
- Expand verification and deployment around the standalone server binary.

### 7. Tighten Verification

- Add clearer support labels everywhere: `supported`, `preview`, `scaffold`.
- Add macOS verification if it is intended to remain an active target.
- Add Web CI coverage.
- Add an Android runtime smoke test on emulator or device farm infrastructure.

## Mid Term

### 8. Asset And Content Pipeline Hardening

- Replace ad-hoc runtime asset probing with explicit asset mount/configuration rules.
- Expand the asset cooker so runtime paths are less dependent on repo-relative layout.
- Make packaged and development builds resolve content the same way.

### 9. Multiplayer And World Streaming

- Expand chunk streaming beyond the current flat-world baseline.
- Harden reconciliation and remote interpolation tooling.
- Separate replication concerns from gameplay-specific controller logic where possible.

## Later

### 10. Reintroduce Deferred Gameplay Systems

- Bring vehicle and aircraft play back as first-class modes after runtime convergence.
- Pair traversal expansion with world streaming and LOD work rather than shipping them as isolated features.

### 11. Future Platforms

- Revisit XR, iOS, and console targets only after:
  - shared runtime services are in place
  - the standalone dedicated server path is stable
  - asset/save path abstractions are real
- Android/Web divergence is materially reduced
