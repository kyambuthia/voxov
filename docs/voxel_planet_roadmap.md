# Voxel Planet Roadmap

This track replaces the debug planet overlay with a traversal-ready streamed
voxel planet. The target is a system where rendering, collision, player
movement, and debug tooling all read from the same planet model.

## Target Architecture

The finished planet system is built around these owned pieces:

- `PlanetDefinition`: stable planet scale, seed, voxel size, and face
  resolution.
- `PlanetQuadtree`: persistent node hierarchy for the six cube-sphere faces.
- `PlanetLODSelector`: camera-driven visible set with bounded LOD changes.
- `PlanetStreamer`: runtime residency manager that turns visible/requested nodes
  into resident chunk meshes with stable mesh IDs and evicts stale chunks.
- `PlanetTerrain`: deterministic terrain sampling and chunk meshing for any
  face/x/y/lod cell.
- `VoxelCollisionWorld`: radial surface queries and raycasts against the same
  height source used by terrain.
- Player/camera traversal: local tangent-frame movement, gravity, stepping,
  snapping, and camera orbit around radial up.

## Milestones

### 1. Radial Traversal Foundation

Status: in progress.

- Use `planet_up_at(position)` for movement, gravity, stepping, grounding, and
  camera orbit.
- Replace world-Y-only planet grounding with radial surface distance/raycast.
- Keep flat-world behavior as the fallback when no planet collider is active.

### 2. LOD-Correct Terrain Chunks

Status: in progress.

- Treat `PlanetChunkId::lod` as the quadtree cell depth.
- Generate each chunk over its actual face UV range.
- Keep fixed local column resolution per chunk so higher LOD gives denser
  world-space geometry.
- Derive visual terrain and collision height from the same deterministic sampler.
- Implement same-face neighbor sampling for face culling.
- Track cross-face seams explicitly until proper face-neighbor transforms land.

### 3. Runtime Streaming And Residency

Status: in progress.

- Add a planet streamer that owns quadtree, selector, resident meshes, and
  generation budgets.
- Generate bounded numbers of requested chunks per frame.
- Expose visible chunk meshes as separate `RenderMesh` entries with stable
  nonzero mesh IDs.
- Let the renderer cache resident chunks and evict meshes absent from the scene.
- Report streamed chunk count through render stats/debug HUD.

### 4. Seam And Collision Fidelity

Status: planned.

- Add cube-face neighbor transforms for cross-face edge sampling.
- Use the same seam logic for visual culling, height queries, and future edit
  propagation.
- Extend collision beyond scalar surface height where gameplay needs true voxel
  side faces, caves, or overhangs.

### 5. Async Generation

Status: planned.

- Move chunk generation/meshing behind job-system tasks.
- Keep main-thread upload bounded and deterministic.
- Add cancellation for chunks no longer requested by the visible set.

### 6. Verification And Tooling

Status: in progress.

- Unit tests for face mapping, quadtree subdivision, LOD chunk coverage, terrain
  determinism, radial surface raycasts, and streamer residency.
- Debug HUD counters for visible/resident/requested/evicted chunks.
- Visual debug overlays for current face, LOD boundaries, and missing seams.

## Non-Goals For This Phase

- Editable/destructible planet voxels.
- Full cave/overhang collision.
- Network replication of planet edits.
- GPU clipmaps or compute meshing.

Those are later layers. This phase is about making the planet coherent,
streamed, and traversable first.
