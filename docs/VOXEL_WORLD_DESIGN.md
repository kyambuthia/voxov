# VOXOV Infinite Voxel World Design (Renderer + Networking + Multiplayer)

## Scope and Assumptions

This document defines a practical baseline architecture for:

1. Infinite-ish procedural voxel planets.
2. Vulkan rendering and streaming.
3. Multiplayer-ready deterministic world generation.
4. Stable frame time and bounded memory.

Assumptions:

1. Near-ground gameplay is voxel/chunk based.
2. Space traversal uses planet-level proxies until descent.
3. Generation is deterministic and chunk-local:
   `(planet_seed, lod, chunk_coord) -> density/material/mesh`.
4. Authoritative server controls game state; clients can generate terrain locally from shared seeds.
5. Initial baseline targets Linux + Vulkan, single GPU queue family or separate transfer queue when available.

---

## 1) Coordinate Systems and Precision

### Spaces

1. `Universe Space` (double precision)
   - Unit: meters.
   - Used for planets, orbital transforms, long-distance travel.
   - Never used directly for per-vertex GPU transforms.

2. `Planet Space` (double precision)
   - Local frame centered on planet core.
   - Axes fixed to planet rotation frame.
   - Used for generation queries and chunk addressing.

3. `Floating Origin Space` (single precision)
   - Player-centric render/sim space near camera.
   - Origin rebased when camera drifts beyond threshold (for example 256 m).
   - Used by physics/render for nearby objects.

4. `Chunk Grid Space` (integer)
   - `chunk_coord = floor(planet_pos / (chunk_size * voxel_size))`.
   - Signed integer 3D coordinates.

5. `Voxel Index Space` (integer)
   - Local voxel index `0..N-1` per chunk axis.

### Precision Strategy

1. Keep planet and player absolute positions in `float64`.
2. For simulation/render each frame:
   - Compute `float32` local transforms relative to floating origin.
3. Rebase origin periodically:
   - Shift all local entities by `-origin_delta`.
   - Keep server/state coordinates in high precision, unaffected.

### Multi-Planet Transitions

1. Maintain active context:
   - `current_planet_id` when inside influence radius.
   - `space_mode` when outside all near-planet thresholds.
2. In space mode:
   - Use universe-space positions and planet proxy rendering.
3. On approach:
   - Switch to target `planet_space` and warm stream chunks before touchdown.

---

## 2) Planet Model

Each planet is deterministic:

```txt
PlanetDefinition {
  planet_id: u64
  seed: u64
  radius_m: f64
  sea_level_m: f32
  gravity_mps2: f32
  atmosphere: { density, falloff, color_rayleigh, color_mie }
  climate: { temp_lapse_rate, moisture_bias, wind_bias }
  noise: { continent, mountain, detail, cave, warp }
  biome_table_id: u32
}
```

### Geometry Model

1. Base SDF: sphere or low-frequency ellipsoid.
2. Terrain displacement: domain-warped FBM + ridged terms.
3. Cheap “erosion-like” shaping:
   - Use warped ridges and terrace functions.
   - Add directional flow mask approximating channels/rivers.
4. Caves/overhangs:
   - 3D cave SDF subtraction using Worley + FBM modulation.
   - Depth/biome masks gate cave probability.

---

## 3) Procedural Generation Pipeline

Pipeline per chunk (pure function, async-safe):

1. Build deterministic RNG/hash from `(planet_seed, lod, chunk_coord)`.
2. Evaluate climate fields at coarse sample points:
   - `temperature(lat, elev, noise)`
   - `moisture(lat, wind_bias, noise)`
3. Evaluate density field at voxel corners or scalar samples.
4. Convert density to solid/empty voxel occupancy.
5. Assign material IDs from biome + slope + sea level + altitude.
6. Optional post-pass:
   - Decor spawners (trees/rocks) from deterministic scatter.

### Noise Stack (recommended baseline)

1. Continental mask: low-frequency FBM.
2. Mountain mask: ridged multifractal.
3. Domain warp: medium-frequency 3D warp.
4. Detail: high-frequency FBM.
5. Caves: 3D Worley + FBM thresholded by depth.

### Biome Selection

Input tuple:

```txt
(temperature, moisture, height_above_sea, slope)
```

Lookup strategy:

1. 2D table: `temperature x moisture -> biome family`.
2. Override by altitude/slope:
   - High altitude + cold -> snow/ice.
   - High slope -> exposed rock.
   - Near sea and hot/dry -> sand.

Output:

```txt
density field + material_id field
```

---

## 4) Chunking and LOD

## Chunk Size and Grouping

Recommended baseline:

1. Simulation chunk: `32^3` voxels.
2. Region: `8x8x8` chunks for metadata indexing and IO/cache grouping.
3. Voxel size near ground: `1.0 m` baseline (tune later to 0.5 m for finer detail).

### LOD Scheme (hybrid, efficient)

1. Near ring (`0..R0`): full voxel chunks, greedy mesh.
2. Mid ring (`R0..R1`): downsampled density chunks meshed at lower resolution.
3. Far ring (`R1..R2`): planet clipmap / spherical patch proxy mesh with macro materials.
4. Space mode: full planet sphere mesh + atmosphere only.

Why:

1. Keeps close gameplay fully voxel.
2. Mid LOD cuts CPU/GPU load.
3. Far/space avoids impossible “infinite chunk” residency.

### Crack-Free LOD

Baseline:

1. Use skirts on lower-detail chunk borders.
2. Quantize vertex positions on LOD boundaries.

Upgrade path:

1. Transvoxel stitching tables for seamless transitions if smooth SDF meshing is used.

### Memory Budget and Residency

Example budgets (desktop baseline):

1. Voxel data CPU budget: 1.0-1.5 GB cap.
2. Mesh CPU cache: 512 MB cap.
3. GPU mesh buffer pool: 512 MB-1 GB.

Eviction:

1. Weighted LRU by:
   - Distance.
   - Visibility.
   - Last access time.
   - Planet relevance.
2. Keep directional prefetch ring biased by velocity vector.

---

## 5) Meshing Strategy (GPU-Friendly)

Recommended baseline: blocky + pretty

1. Greedy meshing for occupied voxels.
2. Face culling against neighbor occupancy.
3. Per-vertex baked AO (4-corner AO per face).
4. Material-atlas index per face or per vertex.

Upgrade path:

1. Keep same density pipeline, add optional smooth extractor for selected biomes.
2. Support dual contouring in mid/far if desired later.

### Vertex/Index Formats

```txt
VertexVoxel {
  pos_q16x3       // quantized local position in chunk
  normal_oct16x2  // octahedral encoded normal
  ao_u8           // ambient occlusion
  material_u16    // material/atlas index
}

Index: u16 for <= 65k vertices per mesh section, else u32 fallback.
```

### Mesh Cache and Update Paths

1. `ChunkMeshCache` keyed by `(planet_id, lod, chunk_coord, mesh_revision)`.
2. Rebuild triggers:
   - Generation complete.
   - Voxel edits.
   - Neighbor change affecting face visibility.
3. Partial update path:
   - Dirty subregion tracking to avoid full remesh when possible.

---

## 6) Multithreading and Job System

Pipeline:

1. `RequestChunk`
2. `GenerateDensity`
3. `FillVoxels/Materials`
4. `MeshChunk`
5. `UploadGPU`
6. `ReadyForRender`

### Chunk State Machine (atomic)

```txt
UNLOADED -> REQUESTED -> GENERATING -> GENERATED -> MESHING -> MESH_READY -> UPLOADING -> RESIDENT
```

Cancellation:

1. If player velocity or planet context changes, mark far/outdated jobs cancelled.
2. Generation and meshing jobs poll cancel token at stage boundaries.

Scheduling priority score:

```txt
priority = w_dist * distance_weight
         + w_view * frustum_weight
         + w_vel  * velocity_alignment
         + w_game * gameplay_importance
```

Implementation:

1. Work-stealing thread pool.
2. MPMC lock-free queues per stage.
3. Main thread only performs state integration and command recording.

---

## 7) Vulkan Resource Plan

### Buffering

1. Large GPU-only mesh buffers with suballocation:
   - Vertex arena.
   - Index arena.
   - Indirect draw command buffer.
2. CPU-visible staging ring buffer.
3. Transfer queue uploads if available, else graphics queue with transfer barriers.

### Draw Submission

1. Build `VkDrawIndexedIndirectCommand` list for visible chunks.
2. Frustum culling baseline on CPU.
3. Optional GPU culling compute pass writing compacted indirect list.

### Descriptors and Materials

1. Texture array or bindless descriptor indexing.
2. Material table in SSBO:
   - Albedo/roughness/normal indices.
   - Parameter scalars.
3. Per-chunk push constants:
   - Chunk origin.
   - LOD level.
   - Material range offset.

---

## 8) Lighting and Visual Quality (Efficient)

Baseline “pretty but cheap”:

1. Directional sun + cascaded shadow maps (limited cascades).
2. Per-vertex AO from mesher.
3. Height/distance fog.
4. Atmospheric scattering approximation:
   - Precomputed LUT or low-cost analytic approximation per pixel.
5. Water:
   - Separate water mesh extraction.
   - Simple normal animation + depth tint + shoreline foam mask.

GI alternative to full voxel GI:

1. Skylight term from hemisphere approximation.
2. Optional sparse probe grid around player for bounce tint.

---

## 9) Planet Hopping and Transition Flow

### Space Mode

1. Render planets as sphere proxy meshes with macro material maps.
2. No voxel chunk residency except small safety bubble around player if needed.

### Descent (planet enter)

1. Trigger threshold at `altitude < enter_voxel_altitude`.
2. Start warm-up streaming:
   - Request near/mid rings under projected landing trajectory.
3. Crossfade:
   - Blend proxy planet surface with voxel terrain over altitude band.

### Ascent (planet leave)

1. Trigger threshold at `altitude > exit_voxel_altitude`.
2. Fade out voxel detail and keep proxy sphere.
3. Aggressively evict chunk data for departed planet (keep small cache only).

---

## 10) Recommended Modules and Interfaces

```txt
PlanetSystem
  - planet registry, seeds, orbital transforms, climate params

CoordinateSystem
  - universe<->planet transforms, floating origin rebasing

ChunkManager
  - residency map, state machine, lifetime, dirty flags

LODSystem
  - ring computation, thresholds, transition policy

GenJobs
  - deterministic density/material generation

MeshJobs
  - greedy meshing, AO, seam handling

GpuUpload
  - staging, suballocation, sync/barriers, residency handles

RenderWorld
  - visibility, indirect command build, pass submission

NetWorldSync
  - seed/planet metadata replication, voxel edits, snapshot hooks
```

---

## 11) Pseudocode

### a) Chunk key hashing and deterministic seeding

```cpp
struct ChunkKey
{
    uint64_t planet_id;
    int32_t lod;
    int32_t cx;
    int32_t cy;
    int32_t cz;
};

uint64_t mix64(uint64_t x)
{
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return x;
}

uint64_t chunk_seed(uint64_t planet_seed, const ChunkKey &k)
{
    uint64_t h = planet_seed;
    h ^= mix64(k.planet_id + 0x9e3779b97f4a7c15ULL);
    h ^= mix64((uint64_t)(uint32_t)k.lod << 32 | (uint32_t)k.cx);
    h ^= mix64((uint64_t)(uint32_t)k.cy << 32 | (uint32_t)k.cz);
    return mix64(h);
}
```

### b) Density function for a planet

```cpp
float density_planet(const PlanetDefinition &p, double3 planet_pos)
{
    double r = length(planet_pos);
    double3 n = planet_pos / max(r, 1e-6);
    double elevation = r - p.radius_m;

    float cont = fbm_low(n * p.noise.continent_freq, p.seed + 11);
    float warp = fbm3(n * p.noise.warp_freq, p.seed + 23);
    double3 q = n + (double3)warp * p.noise.warp_amp;
    float ridge = ridged_fbm(q * p.noise.mountain_freq, p.seed + 37);
    float detail = fbm(q * p.noise.detail_freq, p.seed + 41);

    float terrain_h =
        cont * p.noise.continent_amp +
        ridge * p.noise.mountain_amp +
        detail * p.noise.detail_amp;

    float cave = worley3(planet_pos * p.noise.cave_freq, p.seed + 59);
    float cave_mask = smoothstep(p.noise.cave_min, p.noise.cave_max, cave);

    float solid = (float)(terrain_h - elevation);
    solid -= cave_mask * p.noise.cave_strength;
    return solid; // >0 solid, <=0 empty
}
```

### c) Biome selection

```cpp
BiomeId pick_biome(float temperature, float moisture, float height, float slope)
{
    BiomeId b = biome_table_lookup(temperature, moisture);

    if (height > HIGH_ALTITUDE && temperature < COLD_THRESHOLD) {
        b = BIOME_SNOW;
    }
    if (slope > STEEP_SLOPE) {
        b = BIOME_ROCK;
    }
    if (abs(height) < BEACH_BAND && temperature > HOT_THRESHOLD && moisture < DRY_THRESHOLD) {
        b = BIOME_SAND;
    }
    return b;
}
```

### d) Scheduling and eviction

```cpp
void update_streaming(const PlayerState &player)
{
    auto wanted = lod_system.compute_wanted_chunks(player);

    for (const ChunkKey &k : wanted) {
        if (!chunk_manager.is_resident_or_pending(k)) {
            chunk_manager.request(k, priority_score(k, player));
        } else {
            chunk_manager.bump_priority(k, priority_score(k, player));
        }
    }

    auto victims = chunk_manager.select_eviction_candidates(memory_budget_bytes, player);
    for (const ChunkKey &k : victims) {
        chunk_manager.evict(k);
    }

    job_system.cancel_outdated(player.planet_id, wanted.bounds_hint());
}
```

### e) Meshing entry point

```cpp
MeshData build_chunk_mesh(const ChunkVoxels &vox, const NeighborMask &neighbors)
{
    MeshBuilder mb;
    mb.reserve_estimate(vox.solid_count);

    for (int z = 0; z < CHUNK_N; ++z) {
        for (int y = 0; y < CHUNK_N; ++y) {
            // Greedy run per row/face for fewer quads.
            greedy_emit_row_faces(vox, neighbors, y, z, mb);
        }
    }

    mb.compute_vertex_ao();
    return mb.finalize();
}
```

---

## 12) Data Layout and Compression

### Core Layouts

1. Use SoA for generation buffers:
   - `density[]`, `material[]`, `flags[]` separate arrays.
2. Use compact AoS for final mesh vertex stream.
3. Keep chunk metadata in fixed-size structs, contiguous vector/pool.

### Storage/Compression

1. Voxel chunk storage:
   - Palette + RLE for homogeneous regions.
   - Fallback raw bitmask + material array when high entropy.
2. Empty/full chunk fast path:
   - Single-byte occupancy mode flag avoids allocation of full voxel arrays.
3. No per-voxel heap allocations.

---

## 13) Networking and Multiplayer Integration

### Authoritative Model

1. Server stores authoritative dynamic edits/entities.
2. Procedural terrain base is not streamed as full voxels:
   - Only planet definitions + seeds + generation version hash.
3. Client generates base terrain locally, then applies replicated edits/deltas.

### Replication Payloads

1. `PlanetDefinition` and registry.
2. Chunk edit ops (set voxel, carve sphere, place prefab), versioned.
3. Snapshot entity state independent from chunk generation pipeline.

### Determinism Guarantees

1. Version generation functions with explicit `gen_version`.
2. Include `gen_version` in chunk keys and network protocol.
3. On mismatch, fallback to server mesh patch or deny join with version message.

---

## 14) Recommended Implementation Order

1. Coordinate and floating-origin layer.
2. Chunk key/state machine and job system.
3. Deterministic density/material generation for one planet.
4. Greedy meshing + Vulkan upload + indirect draw.
5. LOD rings + eviction + velocity-biased prefetch.
6. Planet proxy rendering + atmosphere.
7. Planet descent/ascent blending.
8. Network seed replication + chunk edit ops.
9. Performance pass:
   - Profiling.
   - Cache tuning.
   - Optional GPU culling/compute meshing experiments.

---

## 15) Tradeoffs and Baseline Recommendation

### Baseline to Implement First

1. `32^3` chunks.
2. Greedy meshing with AO.
3. CPU frustum culling + indirect draw.
4. 3-tier LOD (near voxel, mid downsampled voxel, far proxy planet).
5. Deterministic generation from `(seed + coords)`.
6. Server replicates seeds + edits only.

Why this baseline:

1. Fast to implement.
2. Predictable performance and memory usage.
3. Clean upgrade path to smoother terrain extraction, GPU culling, and richer lighting.

### Future Enhancements

1. Transvoxel seam stitching.
2. GPU meshing experiments for high-end hardware.
3. Sparse probe GI and better atmospheric LUTs.
4. Planet climate simulation for dynamic weather.
