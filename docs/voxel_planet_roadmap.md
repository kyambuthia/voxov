# Voxel Planet Roadmap

This track covers the streamed spherical voxel planet system where rendering,
collision, player movement, and debug tooling read from the same planet model.

## Current Architecture (Verified in `src/`)

- `PlanetDefinition`, `PlanetQuadtree`, `PlanetLODSelector`, and
  `PlanetStreamer` (`src/engine_world/planet.*`): cube-sphere quadtree node
  hierarchy, camera-driven LOD selection, and bounded chunk residency/eviction.
- `BlockWorld` (`src/engine_world/planet_blocks.*`): chunked spherical voxel
  terrain with deterministic generation, mesh building, and on-demand per-body
  runtimes in `Engine` (`Voxov`, `Luna`, `Aster`).
- `PlanetLODSystem` (`src/engine_world/planet_lod.*`): screen-space LOD
  selection for distant terrain globes (`append_celestial_meshes`).
- `VoxelCollisionWorld` (`src/engine_world/physics/voxel_collision.*`): radial
  surface queries and raycasts (`block_picking.*`) against the active planet.
- Player/camera traversal (`src/engine_gameplay/player/`): radial tangent-frame
  movement (`surface_orientation.hpp`), gravity, stepping, snapping, and
  first/third-person camera orbit around radial up.

## Milestones

### 1. Radial Traversal Foundation

Status: implemented.

- Uses radial up (`planet_up_at` / `surface_orientation.hpp`) for movement,
  gravity, stepping, grounding, and camera orbit.
- Supports radial surface collision and raycasts alongside flat-world fallback.

### 2. LOD-Correct Terrain Chunks

Status: implemented.

- `PlanetQuadtree` / `PlanetLODSelector` and `BlockWorld` sample deterministic
  terrain over cube-sphere faces and render distant bodies via cached terrain
  globes with screen-space LOD.

### 3. Runtime Streaming And Residency

Status: implemented (synchronous on render thread).

- `PlanetStreamer` and `Engine::stream_block_terrain` manage bounded chunk
  generation, mesh caching, and eviction, with neighbor-chunk invalidation on
  block edits (`invalidate_block_meshes`).
- Client-side single-player block edits (`1`-`7` hotbar) and persistence are
  implemented via `PersistentGameState`.

### 4. Seam And Collision Fidelity

Status: planned.

- Unify cube-face neighbor transforms for cross-face edge sampling/culling.
- Extend collision beyond scalar/surface height where gameplay needs full
  cave/overhang voxel collision and server-authoritative edit validation.

### 5. Async Generation

Status: planned.

- Move chunk generation and meshing out of `Engine::stream_block_terrain` into
  bounded `engine_core/jobs.*` worker queues with cancellation for stale
  requests and bounded main-thread GPU upload batches.

### 6. Verification And Tooling

Status: implemented (ongoing).

- Unit tests in `src/tests/test_main.cpp` cover cube-face mapping, quadtree
  subdivision, LOD chunk coverage, terrain determinism, radial raycasts, and
  streamer residency.
- HUD/debug stats expose streamed and resident chunk metrics.

## Remaining Non-Goals / Deferred Layers

- Network replication of arbitrary voxel edits (tracked in
  `docs/SCALABILITY_ROADMAP.md` under P0 server/client world unification).
- GPU clipmaps or compute meshing.
