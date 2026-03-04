# VOXOV Implementation Guide

This document is the execution-order guide for the current codebase and should match the source tree at all times.

## Ground rules

- Keep build outputs under `./build/<target>/...`.
- Prioritize correctness and observability before feature expansion.
- Keep desktop and Android multiplayer behavior converging.
- Prefer small vertical slices with validation at each step.

## Current baseline (source-verified)

### Runtime paths

- Desktop: `src/game/main.cpp` + shared `Engine` (`src/engine/engine.cpp`)
- Android: `src/game/android_main.cpp` native runtime
- Web: `src/game/web_main.cpp` preview target

### Multiplayer baseline

- Authoritative ENet server/client with protocol header and version checks
- Fixed server simulation tick and bounded send rates
- Client reconciliation for local player
- Remote snapshot interpolation on desktop and Android
- `PlayerRemove` replication on disconnect
- Net counters (pps/Bps/invalid packets/snapshot rates) surfaced in HUD/logs

### Tooling baseline

- Unit/integration tests in `voxov_tests`
- Net stress harness in `voxov_net_stress`
- Release workflow bundles runnable Linux/Windows artifacts and validates startup

## Phase plan

## Phase 0 - Source-of-truth cleanup (complete)

Objective: remove stale guidance and keep docs aligned with implementation.

Completed:

1. Updated Android docs to reflect active gameplay + multiplayer runtime.
2. Updated build docs to use `./build/<target>/...` paths.
3. Updated release docs to describe runnable bundled artifacts and smoke validation.
4. Updated architecture/vision notes to reflect near-term traversal scope gating.

## Phase 1 - Protocol and stress hardening (complete for this slice)

Objective: harden wire format and increase regression coverage.

Completed:

1. Extended packet header with explicit flags/sequence and 16-bit payload size.
2. Added payload upper-bound validation guardrails.
3. Added packet sequence stamping on client/server outbound packets.
4. Expanded `voxov_net_stress` to scenario matrix (2/8/16/32 clients).
5. Added forced disconnect/reconnect cycle assertions in stress tests.
6. Added unit test coverage for packet header validation.

Next hardening backlog:

- Quantization and bandwidth reduction for high-rate state.
- Per-message stream sequencing/ordering policies where needed.

## Phase 2 - Desktop/Android multiplayer convergence (partial)

Objective: reduce duplicate multiplayer logic between runtimes.

Completed:

1. Extracted shared remote interpolation/sample buffering helper (`src/engine_net/remote_interp.hpp`).
2. Wired both desktop and Android runtimes to shared interpolation logic.

Remaining:

- Extract shared local prediction/reconciliation utilities.
- Extract shared net-input packing and remote-state ingest paths.

## Phase 3 - Android LAN reliability (complete for this slice)

Objective: improve LAN discovery reliability on Android Wi-Fi networks.

Completed:

1. Added native-side `WifiManager.MulticastLock` lifecycle management via JNI.
2. Lock now tracks host/join discovery state and is released when discovery ends.
3. Existing manifest multicast permission is retained.

Remaining:

- Add explicit discovery diagnostics counters to UI text for host/join UX debugging.

## Phase 4 - Traversal scope discipline (complete)

Objective: keep near-term multiplayer milestones stable while preserving long-term traversal goals.

Status:

- On-foot traversal remains the default multiplayer milestone path.
- Vehicle/aircraft traversal remains intentionally gated/deferred during current hardening window.
- Vision/architecture docs now explicitly state this near-term scope decision.

## Phase 5 - Release pipeline alignment (complete for this slice)

Objective: enforce packaging/runtime policy in CI and docs.

Completed:

1. Release workflow desktop builds moved to policy-compliant paths under `build/desktop/...`.
2. Artifact packaging paths updated accordingly.
3. Docs aligned to bundled artifact policy and smoke-test expectations.

Remaining:

- Extend release/runtime smoke checks as platform coverage expands (macOS/web/XR where applicable).

## Next execution queue (after this guide update)

1. Shared prediction/reconciliation helper extraction for desktop + Android.
2. Protocol bandwidth optimization (quantized transforms/anim params).
3. Interest management refinement for larger sessions.
4. Additional net tests for packet fuzz/invalid length rejection.
