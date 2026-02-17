# AVBD Integration Plan (VOXOV)

## Summary

This document evaluates adopting **Augmented Vertex Block Descent (AVBD)** for VOXOV physics, with emphasis on mobile viability and cross-platform delivery.

References:
- Project page: https://graphics.cs.utah.edu/research/projects/avbd/
- Demo source (2D reference): https://github.com/savant117/avbd-demo2d

## What AVBD Gives Us

From the published project description, AVBD extends Vertex Block Descent with an augmented Lagrangian formulation to improve:

1. Handling of hard constraints without instability.
2. Convergence under high stiffness ratios.
3. Contact-heavy scenarios (stacking, friction, articulated constraints).
4. Parallel performance with GPU-oriented implementations.

## Can We Implement It In VOXOV?

Yes, but not as a quick drop-in replacement.

Practical expectation:

1. We can integrate AVBD in phases as an optional solver backend.
2. We should keep current gameplay working with existing controller/collision while AVBD matures.
3. Early scope should target specific systems first (vehicle suspensions, rope/soft interactions, complex contact islands), not all physics at once.

## Is AVBD Possible On Mobile?

Yes, with profile-based constraints.

Mobile feasibility guidance:

1. Use smaller iteration counts and tighter per-frame solver budgets.
2. Limit active dynamic body count in the camera neighborhood.
3. Keep broadphase/contact generation cheap and capped.
4. Prefer SoA data layouts and preallocated pools (no per-body heap churn).
5. Use task-based CPU parallelism first; treat GPU AVBD as a later optimization phase.

Conclusion:

1. AVBD is mobile-feasible for bounded scenes.
2. "Millions of interacting objects" is not a realistic default target for phone runtime gameplay in VOXOV.

## Recommended Architecture In VOXOV

Add solver abstraction under `engine_physics`:

1. `IPhysicsSolver`
2. `JoltSolver` (existing baseline)
3. `AvbdSolver` (new, experimental)

Required supporting modules:

1. `engine_physics/constraints/*` for joints/attachments/contact constraints.
2. `engine_physics/contact_graph/*` for persistent contact sets and islands.
3. `engine_physics/avbd/*` for AVBD iteration kernels and data buffers.
4. `engine_core/jobs` integration for parallel task scheduling.

Runtime selection:

1. `--physics-solver=jolt|avbd`
2. Platform profile gate `mobile_low`.
3. Platform profile gate `mobile_high`.
4. Platform profile gate `desktop`.
5. Platform profile gate `console`.

## Multiplayer Implications

For authoritative networking:

1. Server remains authority for dynamic-body truth.
2. AVBD should run server-side first.
3. Clients use interpolation/prediction and receive corrected snapshots.
4. Do not depend on strict bitwise determinism across different CPUs/GPUs.

## Phase 0: Guardrails

1. Keep current gameplay stable with Jolt/custom voxel controller.
2. Add solver interface + runtime toggle with identical API.
3. Add `solver_ms` telemetry.
4. Add `active_bodies` telemetry.
5. Add `contact_count` telemetry.
6. Add `iteration_count` telemetry.

## Phase 1: AVBD Prototype (CPU)

1. Integrate minimal AVBD core for rigid-body constraints in isolated test scene.
2. Validate stack stability tests.
3. Validate friction stability tests.
4. Validate high-stiffness joint tests.
5. Ship behind `VOXOV_EXPERIMENTAL_AVBD`.

## Phase 2: Hybrid Gameplay Use

1. Keep player capsule/controller on existing path.
2. Enable AVBD for vehicle stacks.
3. Enable AVBD for constrained props.
4. Enable AVBD for rope/soft interactions.
5. Add fallback to Jolt per scene/profile.

## Phase 3: Mobile Tuning

1. Define max active dynamic bodies per device class.
2. Define max constraints per device class.
3. Define max iterations per device class.
4. Add adaptive quality by reducing iterations during frame pressure.
5. Add adaptive quality by sleeping/disabling far-away dynamic islands.
6. Validate thermal behavior over long play sessions.

## Phase 4: GPU Path (Optional)

1. Evaluate compute backend only after CPU path is production-stable.
2. Keep CPU fallback mandatory for compatibility and debugging.

## Not Recommended Right Now

1. Full immediate replacement of all physics with AVBD.
2. Shipping first AVBD version as mandatory on mobile.
3. Binding gameplay-critical progression to experimental solver behavior.

## Recommended Baseline Decision

1. Keep current stack for shipping stability.
2. Integrate AVBD incrementally as an optional solver backend.
3. Target desktop first, then mobile profile rollout.
4. Use per-platform budgets and fallback paths as non-negotiable for true cross-platform support.
