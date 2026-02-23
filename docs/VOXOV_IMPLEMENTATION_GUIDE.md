# VOXOV Implementation Guide

This is the working implementation guide for VOXOV based on:
- the current source tree (desktop + Android + web split runtime paths)
- the imported audit/plan in `docs/voxov.md`
- recent networking fixes already applied in this repo (init/connect failure handling, player removal replication, Android remote rendering, net stress harness)

The goal is to give a practical sequence to follow while implementing multiplayer, Android parity, and later ragdolls/procedural locomotion without breaking the existing playable paths.

## What This Guide Optimizes For

- Fix correctness before adding complexity
- Keep desktop and Android behavior converging (not diverging further)
- Preserve fast iteration (small patches, measurable wins)
- Add tests/instrumentation alongside features

## Current Baseline (As of This Guide)

### Runtime structure

- Desktop uses shared `Engine` path (`src/engine/engine.cpp`, `src/game/main.cpp`)
- Android uses a separate runtime in `src/game/android_main.cpp` (EGL/GLES + custom loop)
- Web is MVP/bootstrap (`src/game/web_main.cpp`)

### Multiplayer baseline

- ENet client/server + LAN discovery exist
- Client/server init/connect/server-start failure paths now return `bool` and are handled
- `PlayerRemove` replication exists (remote players clean up on disconnect)
- Server player-state broadcast is rate-limited (basic throttle)
- Android now consumes and renders replicated remote players
- Net stress harness exists: `src/tests/net_stress.cpp` (`voxov_net_stress`)

### Known major remaining issue

- Server simulation is still packet-driven in `NetServer::pump()` (movement advances when input packets arrive), not on an independent fixed server tick.

## Rules For Implementation Work

- Build outputs must stay under `./build/<target>/...`
- Prefer small vertical slices with validation after each step
- Do not couple new features to web transport yet (web networking can stay deferred)
- Add debug counters/HUD metrics before tuning behavior

## Implementation Roadmap (Order to Follow)

## Phase 0: Stabilize and Measure (Immediate)

Objective: make networking behavior observable before larger refactors.

### Tasks

1. Add server/client net counters
- packets/sec and bytes/sec by direction
- snapshot sends/sec
- player-state broadcasts/sec
- dropped/invalid packet counts

2. Surface counters in logs and dev HUD
- Desktop: dev HUD (`src/engine/engine.cpp`)
- Android: on-screen/net hint logs (`src/game/android_main.cpp`)

3. Expand `voxov_net_stress`
- variable client counts (2, 8, 16, 32)
- packet send cadence variation
- disconnect/reconnect cycle assertions

### Touchpoints

- `src/engine_net/net_server.cpp`
- `src/engine_net/net_client.cpp`
- `src/engine/engine.cpp`
- `src/game/android_main.cpp`
- `src/tests/net_stress.cpp`

### Acceptance Criteria

- Server logs stable pps/bytes once per second
- Android and desktop show remote count and connection state clearly
- Stress harness reports pass/fail reasons clearly (not silent hangs)

## Phase 1: Fix Server Tick Ownership (Highest Priority)

Objective: server simulation runs at a fixed rate independent of packet arrival.

### Why First

This is the foundation for:
- consistent movement
- reasonable bandwidth
- meaningful client prediction/reconciliation
- reproducible debugging

### Tasks

1. Split `NetServer::pump()` responsibilities
- network event service (receive/connect/disconnect)
- fixed simulation tick (e.g. 60 Hz)
- snapshot/player-state send tick (e.g. 20-30 Hz)

2. Store latest input per player
- update on `Input` packet receive
- simulate every server tick using last known input

3. Add explicit timing state to `NetServer`
- `last_sim_time`
- `last_snapshot_send_time`
- `last_player_state_send_time`
- clamp catch-up ticks to avoid spiral under stalls

4. Keep chunk replication event-driven/reliable
- no per-tick chunk spam

### Touchpoints

- `src/engine_net/net_server.hpp`
- `src/engine_net/net_server.cpp`
- `src/game/main.cpp` (headless loop expectations/logs)
- `src/engine/engine.cpp` (if local embedded server assumptions need updates)
- `src/game/android_main.cpp` (local host path)

### Acceptance Criteria

- Headless server pps no longer scales with loop speed
- Movement remains stable under client packet jitter
- Snapshot/player-state send rates are bounded and configurable

## Phase 2: Client Prediction + Reconciliation (Desktop First, Then Android)

Objective: local player remains responsive while respecting server authority.

### Tasks

1. Add client history ring buffer
- `(tick, input, predicted_state)`
- enough capacity for RTT + jitter margin

2. Apply authoritative snapshots to local player flow
- compare predicted vs authoritative at snapshot tick
- rewind/replay when error exceeds threshold

3. Add debug modes
- `snap_to_authority` (hard overwrite each snapshot)
- `reconciliation_off` (for comparison)
- error metrics (pos/vel delta)

4. Keep thresholds configurable
- position threshold
- velocity threshold
- correction smoothing options

### Touchpoints

- `src/engine/engine.cpp` (primary integration in shared fixed loop)
- `src/engine_net/net_client.hpp`
- `src/engine_net/net_client.cpp`
- `src/engine_gameplay/player/player_controller.cpp` (if state extraction helpers are needed)
- `src/game/android_main.cpp` (mirror behavior after desktop path proves out)

### Acceptance Criteria

- Desktop local movement remains responsive at normal RTT
- Snapshot correction reduces long-term divergence
- Reconciliation metrics visible in debug output

## Phase 3: Remote Snapshot Interpolation (Desktop + Android Parity)

Objective: replace ad-hoc smoothing with buffered interpolation for remote players.

### Tasks

1. Add sequencing/versioning support to protocol
- protocol header with version
- message sequence number (or per-stream sequence)
- reject stale/out-of-order updates safely

2. Store remote snapshot history per player
- deque/ring buffer of recent `PlayerState` samples

3. Render remote players slightly in the past
- interpolation delay (start with 100-150ms)
- interpolate between surrounding samples
- limited extrapolation fallback only when necessary

4. Preserve animation replication parameters
- continue using compact locomotion params (`anim_state`, `anim_phase`, `anim_blend`)

### Touchpoints

- `src/engine_net/net_common.hpp`
- `src/engine_net/net_client.cpp`
- `src/engine_net/net_server.cpp`
- `src/engine/engine.cpp`
- `src/game/android_main.cpp`

### Acceptance Criteria

- Noticeably reduced hitching on remote players under induced jitter
- Out-of-order packets do not cause backward snaps
- Android and desktop remote movement behavior is comparable

## Phase 4: Android Multiplayer Hardening

Objective: make Android multiplayer reliable enough for regular device testing.

### Tasks

1. LAN discovery reliability
- acquire/release `WifiManager.MulticastLock` around discovery usage
- add diagnostics for discovery send/receive counts
- verify manifest permissions for multicast mode

2. Error surfacing and UX polish
- clearer join/host failure reasons
- visible state transitions (resolving, connecting, connected, failed)

3. Networking parity checks
- confirm Android uses same remote interpolation and player removal behavior as desktop
- verify reconnect/disconnect cleanup

4. Performance guardrails
- cap expensive render/debug work when remote counts rise
- keep GLES path stable with multiple remotes visible

### Touchpoints

- `src/game/android_main.cpp`
- Android Java/Kotlin glue / manifest files under `android/` (multicast lock integration)
- `src/engine_net/lan_discovery.cpp` (if diagnostics/helpers are added)

### Acceptance Criteria

- Android can host/join on LAN and visibly see remote players
- “Join Nearby” works on supported devices more consistently
- Failures are visible instead of silent

## Phase 5: Protocol Hardening and Bandwidth Work

Objective: make the protocol safer and cheaper before adding more replicated features.

### Tasks

1. Add protocol header
- magic
- version
- message type
- sequence number
- payload length

2. Replace unsafe memcpy assumptions where needed
- strict packet size validation
- explicit packing/endianness policy

3. Quantize frequently sent state
- positions/velocities
- animation params if useful

4. Interest/broadcast refinements
- avoid sending all player states to everyone if unnecessary (future)

### Touchpoints

- `src/engine_net/net_common.hpp`
- `src/engine_net/net_client.cpp`
- `src/engine_net/net_server.cpp`
- `src/tests/net_stress.cpp`
- future protocol unit tests in `src/tests/`

### Acceptance Criteria

- Invalid packet lengths are rejected safely
- Protocol version mismatch is detected cleanly
- Measured bandwidth drops vs baseline at same perceived smoothness

## Phase 6: Shared Runtime Convergence (Android Toward `Engine`)

Objective: reduce duplicated gameplay/network logic between desktop and Android.

### Strategy

Do not rewrite Android rendering first. Converge gameplay/network logic first.

### Tasks

1. Extract shared simulation/net integration helpers from `src/game/android_main.cpp`
- local player update
- remote player net state processing
- menu/network action handling (where practical)

2. Reuse shared input shape
- move Android touch output toward `InputState` compatibility
- evaluate use of existing `platform/android/input_android.*`

3. Define a smaller shared mobile-friendly engine layer
- keeps renderer/platform-specific code separate
- centralizes multiplayer logic

### Touchpoints

- `src/game/android_main.cpp`
- `src/engine/engine.cpp`
- `src/platform/android/input_android.cpp`
- new shared helper modules (likely under `src/engine/` or `src/engine_gameplay/`)

### Acceptance Criteria

- Fewer duplicated net/gameplay code paths between desktop and Android
- New multiplayer fixes can be applied once for both platforms

## Phase 7: Ragdolls (After Networking Foundation Is Stable)

Objective: add server-authoritative ragdoll behavior using Jolt without overwhelming bandwidth.

### Tasks

1. Local ragdoll prototype (no networking)
- Jolt ragdoll setup + transitions
- enter/exit ragdoll
- get-up path

2. Network event replication
- `EnterRagdoll`, `ExitRagdoll`, `ApplyImpulse` (reliable)

3. Sparse ragdoll correction replication
- low-rate root + key bodies (unreliable)
- client smoothing/correction

### Touchpoints

- `src/engine_physics/...` or Jolt integration modules
- `src/engine_net/net_common.hpp`
- `src/engine_net/net_server.cpp`
- `src/engine_net/net_client.cpp`
- character/render integration codepaths

### Acceptance Criteria

- Ragdoll transitions are stable locally
- Remote ragdolls look coherent without per-bone high-rate replication

## Phase 8: Procedural Locomotion (Skeleton-First)

Objective: improve character motion quality using compact replicated parameters, not bone spam.

### Tasks

1. Expand procedural locomotion model
- explicit foot contact events
- pelvis offsets
- gait phase control

2. Add terrain-aware foot placement
- voxel raycast-based targets
- two-bone IK legs
- pelvis adjustment when out-of-reach

3. Blend locomotion/ragdoll/get-up states
- add `Fall`, `Land`, `Ragdoll`, `GetUp`

4. Skinned mesh driving (later)
- support externally supplied pose/joint transforms in render path

### Touchpoints

- `src/engine_gameplay/player/player_controller.cpp`
- `src/engine_render/skeletal_animator.*`
- `src/engine_assets/skinned_model.*`
- `src/engine_world/physics/voxel_collision.cpp`

### Acceptance Criteria

- Procedural locomotion works with skeleton-only debug rig first
- Network replication remains compact (parameters/events, not raw bones)

## Testing and Validation Plan (Use Every Phase)

## Build Commands (Repo Policy)

```bash
cmake -S . -B ./build/desktop/main
cmake --build ./build/desktop/main --parallel

cmake -S . -B ./build/desktop/tests -DVOXOV_BUILD_TESTS=ON
cmake --build ./build/desktop/tests --parallel
ctest --test-dir ./build/desktop/tests --output-on-failure
```

## Net Stress Harness

```bash
cmake -S . -B ./build/desktop/tests -DVOXOV_BUILD_TESTS=ON
cmake --build ./build/desktop/tests --target voxov_net_stress --parallel
./build/desktop/tests/bin/voxov_net_stress
```

Note: in restricted sandboxes, ENet socket creation may fail even if the harness builds correctly.

## Manual Multiplayer Smoke Matrix

Run this matrix after any network protocol or tick-scheduling change:

1. Desktop headless server + 2 desktop clients
2. Desktop local host + 1 desktop client
3. Desktop host + Android client
4. Android host + desktop client
5. Disconnect/reconnect remote player
6. Join Nearby (LAN discovery) on Android and desktop

For each run, record:
- connect success/failure
- visible remote count
- average pps/bytes
- jitter symptoms
- snapshot/reconciliation error metrics (when added)

## Implementation Checklist (Short Version)

- [ ] Net counters + HUD/log visibility
- [ ] Fixed server sim tick + bounded send ticks
- [ ] Desktop client prediction + reconciliation
- [ ] Remote snapshot interpolation (desktop + Android)
- [ ] Protocol header/version/sequence + packet validation
- [ ] Android multicast lock + LAN discovery diagnostics
- [ ] Shared runtime convergence (Android net/gameplay logic)
- [ ] Jolt ragdoll local prototype
- [ ] Ragdoll network events + sparse corrections
- [ ] Procedural locomotion + foot IK + state blending

## Working Notes

- Keep `docs/voxov.md` as the long-form audit/reference.
- Use this file as the execution plan and update phase status as implementation progresses.
- Prefer shipping small, verified steps instead of a single large networking rewrite.
