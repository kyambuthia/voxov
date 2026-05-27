# Voxel Rendering Research Notes

These notes summarize online reference material relevant to Voxov's terrain
rendering path. They are architecture inputs, not completed implementation
claims. The current playable terrain uses block voxels generated into greedy
triangle meshes and streamed around the player through
`src/engine_world/flat_world_streamer.*`.

## Current Fit

Voxov currently has height-derived block terrain with planned hills, valleys,
and rivers. Nearby terrain needs collision and potential future editing;
distant terrain primarily needs stable silhouettes and continuous coverage.

The practical rendering direction is therefore:

```text
near field: greedy-meshed voxel chunks with collision
mid/far field: lower-detail terrain representation
streaming: asynchronous generation/meshing with bounded main-thread upload
```

A full sparse-voxel ray-casting renderer is not the next step for this terrain
path. It would replace substantial rendering, streaming, editing, and collision
assumptions before the existing chunk pipeline has been optimized.

## Technique Comparison

| Technique | What it improves | Relevance to Voxov | Decision |
| --- | --- | --- | --- |
| Greedy meshing | Reduces block surfaces to larger coplanar quads, lowering vertex/index and draw workload | Matches the block-voxel presentation already in use | Retain for near terrain |
| Threaded chunk generation and meshing | Prevents generation and triangulation work from blocking frame traversal | Directly addresses visible streaming stalls | Implement next |
| Predictive residency and bounded upload | Requests terrain before arrival and prevents uploads from consuming an entire frame | Addresses player movement pop-in and spikes | Implement with async streaming |
| Geometry clipmaps / coarse heightfield LOD | Renders very large height-derived terrain cheaply at distance | Strong match for hills, valleys, and river beds outside the editable near radius | Evaluate for far terrain |
| Volumetric chunk LOD with transition meshes | Permits distant lower-resolution surface meshes without cracks | Valuable if caves, overhangs, or smooth volumetric terrain become central | Defer until volumetric requirements exist |
| Sparse voxel octree ray casting | Provides compact hierarchical ray traversal for large mostly static voxel detail | High rewrite cost and less natural fit for near dynamic block gameplay | Do not prioritize |
| GigaVoxels-style ray-guided streaming | Streams very large volumetric datasets based on rendering feedback | Research-scale renderer and data-pipeline shift | Do not pursue for the current milestone |

## Findings From References

### Greedy Meshing

Mikola Lysenko's meshing analysis compares naive cube output, culled faces,
and greedy meshing. For block terrain, greedy meshing reduces contiguous
surface regions into larger quads while maintaining the block surface. The
tradeoff is more meshing work when a chunk changes.

**Voxov implication:** Keep greedy meshing for resident near chunks, but move
the expensive generation and meshing work away from the render/update thread.
Meshing should operate on immutable chunk snapshots and publish completed mesh
results for integration.

### Threaded Streaming Practice

Voxel Tools documents chunk-oriented procedural generation and terrain systems
that perform heavy loading, generation, and meshing work in background tasks.
Voxel Plugin documents player-centered "invokers", dynamic LOD, transition
meshes, and prediction for fast-moving players.

**Voxov implication:** The player position should drive requested residency,
including a movement-ahead prefetch band. Camera frustum visibility should
control drawing, not whether terrain is available. Finished worker results
should enter a bounded main-thread/GPU upload queue.

### Geometry Clipmaps

Geometry clipmaps represent terrain as nested regular grids centered on the
viewer, with increasingly coarse spacing farther away. GPU Gems 2 reports this
approach handling very large heightfields while incrementally updating small
regions as the viewer moves. Boundary blending prevents visible LOD popping.

**Voxov implication:** Much of the current generated terrain is a height
surface. A clipmap or similarly coarse far-surface mesh can provide distant
hills, valleys, and rivers without keeping distant terrain at full voxel mesh
resolution. Near editable or collidable space remains voxel chunks.

### Volumetric LOD And Transvoxel

Eric Lengyel's Transvoxel algorithm provides transition cells between
different-resolution voxel meshes produced for volumetric surfaces, avoiding
cracks at LOD boundaries. It is especially relevant to Marching
Cubes/isovalue-derived terrain.

**Voxov implication:** Transvoxel is not required for the current cubic greedy
mesh terrain. Revisit it when gameplay requires smooth volumetric caves,
overhangs, excavation, or planet terrain whose render representation is no
longer adequately height-derived.

### Sparse Voxel Ray Casting

Laine and Karras describe a compact sparse voxel octree and GPU ray traversal
for highly detailed, mostly static voxel surfaces. Crassin et al. describe
GigaVoxels, coupling adaptive volumetric data production and streaming to
rendering feedback for datasets far exceeding GPU memory.

**Voxov implication:** These systems are useful reference points for a future
large static-volume renderer, but neither is a contained improvement to the
current triangle-mesh chunk pipeline. They introduce new GPU data structures,
streaming ownership, ray traversal, shading, editing, and collision questions.

## Recommended Architecture

### Near Terrain

- Keep greedy-meshed block voxel chunks within interaction and collision range.
- Keep voxel data authoritative for collision and edits.
- Generate and mesh chunks from deterministic snapshots on worker threads.
- Publish immutable completed meshes with stable chunk mesh IDs.

### Residency And Integration

- Maintain a loaded radius larger than the immediately visible/interactive
  radius.
- Add eviction hysteresis so walking across a chunk boundary does not
  repeatedly remove and rebuild nearby meshes.
- Predict requests ahead of player velocity for traversal.
- Limit per-frame GPU mesh uploads and resident-set swaps.
- Keep the previous or lower-detail representation visible until a requested
  replacement mesh is ready.

### Far Terrain

- Add a coarse representation for terrain outside the near voxel radius.
- Prefer a heightfield/clipmap-like representation while terrain remains
  fundamentally column-height based.
- Share the deterministic terrain sampler with near chunks so silhouettes and
  river placement agree across representations.
- Blend or skirt near/far boundaries to avoid holes during LOD transitions.

### Future Fully Volumetric Terrain

- Introduce volumetric LOD only when caves, overhangs, destruction, or
  planet-scale detail require it.
- At that time, evaluate transition-mesh schemes such as Transvoxel against
  the desired blocky versus smooth visual style.

## Implementation Order

### Phase 1: Measure The Present Pipeline

Add runtime metrics for:

- CPU time spent generating voxel chunks.
- CPU time spent greedy-meshing chunks.
- CPU/GPU integration or upload time.
- Requested, queued, completed, uploaded, and resident chunk counts.
- Per-chunk and total visible vertex/index counts.
- Frame-time percentiles while traversing.

Exit criterion: traversal stalls can be assigned to generation, meshing,
upload, draw submission, or GPU rendering with evidence.

### Phase 2: Async Flat-World Streaming

Introduce a job pipeline around `FlatWorldStreamer`:

```text
request -> worker generate voxel snapshot -> worker greedy mesh
        -> completed queue -> bounded main-thread upload/residency swap
```

Constraints:

- No chunk job may mutate resident data used by collision or rendering.
- Completed results must be discarded if a newer request supersedes them.
- The render thread must not block waiting for a chunk job.
- Terrain already on screen remains drawable until replacement data is ready.

Exit criterion: traversal performs no synchronous chunk generation or greedy
meshing in the frame update path.

### Phase 3: Residency Quality

- Add prefetch based on player velocity and movement direction.
- Add eviction margin/hysteresis.
- Prioritize collision-ready nearby chunks before purely visual far chunks.
- Add debug visualization for requested, building, resident, and evicting
  chunks.

Exit criterion: normal traversal does not expose unloaded terrain within the
intended view radius.

### Phase 4: Far-Terrain LOD

- Prototype a coarse height-surface or clipmap representation from the shared
  terrain sampler.
- Keep the near voxel/collision radius independent from far visual coverage.
- Include river course representation in both near and far terrain sampling.

Exit criterion: large visible terrain ranges no longer require full-detail
greedy voxel meshes for every distant region.

## Source Index

| Source | Relevance |
| --- | --- |
| Mikola Lysenko, *Meshing in a Minecraft Game* - <https://0fps.net/2012/06/30/meshing-in-a-minecraft-game/> | Block-terrain greedy meshing rationale and tradeoffs |
| Asirvatham and Hoppe, *Terrain Rendering Using GPU-Based Geometry Clipmaps*, GPU Gems 2 - <https://developer.nvidia.com/gpugems/gpugems2/part-i-geometric-complexity/chapter-2-terrain-rendering-using-gpu-based-geometry> | GPU-centered large heightfield LOD and incremental updates |
| Losasso and Hoppe, *Geometry Clipmaps: Terrain Rendering Using Nested Regular Grids* - <https://hhoppe.com/geomclipmap.pdf> | Original geometry clipmap design |
| Eric Lengyel, *The Transvoxel Algorithm for Voxel Terrain* - <https://transvoxel.org/> | Seamless transitions between volumetric terrain LOD meshes |
| Laine and Karras, *Efficient Sparse Voxel Octrees* - <https://research.nvidia.com/publication/2010-02_efficient-sparse-voxel-octrees> | GPU ray-cast sparse voxel hierarchy reference |
| Crassin et al., *GigaVoxels: Ray-Guided Streaming for Efficient and Detailed Voxel Rendering* - <https://www.icare3d.org/research-cat/publications/gigavoxels-ray-guided-streaming-for-efficient-and-detailed-voxel-rendering.html> | Render-feedback-driven streaming of very large volumetric data |
| Voxel Tools documentation, *Overview* - <https://voxel-tools.readthedocs.io/en/latest/overview/> | Background threaded terrain workflow in a practical engine |
| Voxel Tools documentation, *Generators* - <https://voxel-tools.readthedocs.io/en/latest/generators/> | Chunk-oriented procedural generation |
| Voxel Plugin documentation, *World Size and Level Of Details* - <https://docs.voxelplugin.com/1.2/core-systems/voxelworld/world-size-and-level-of-details> | Player-centered LOD, transitions, and predictive invokers |
| Voxel Plugin documentation, *Performance and Profiling* - <https://docs.voxelplugin.com/1.2/technical-notes/performance-and-profiling.html> | Profiling, task priority, meshing and collision work ordering |
