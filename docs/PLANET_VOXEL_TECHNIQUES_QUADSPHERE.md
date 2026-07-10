# Voxel Planet Techniques — Bowerbyte (2025) / Jordan Peck (2015)

**Source**: [bowerbyte.com/posts/blocky-planet](https://www.bowerbyte.com/posts/blocky-planet/)
**Reference**: [jordanpeck.me/2015/02/voxel-planet](https://jordanpeck.me/2015/02/voxel-planet/)

Unity C# prototype. Quad-sphere planet with Minecraft-style cubic voxels, procedural
generation, full destructibility, >20 block types, gravity, and player flight.

---

## 1. Quad Sphere with Reduced Distortion

Standard cube→sphere normalization (project each vertex to unit sphere) produces
significant quad distortion at face edges and corners. Bowerbyte improved this by
**pre-distorting** the flat grid to counteract the normalization distortion.

Result: blocks look square everywhere, not squished into rectangles near sector
boundaries. Pre-distortion parameters chosen to preserve area, angles, and side
lengths of original squares. Visual comparison in the article shows dramatic
improvement at face boundaries.

**VOXOV relevance**: We use `face_uv_to_direction()` and `cubed_sphere_distortion_factor()`
(φ=1.618 at corners). Bowerbyte's approach suggests an improvement: pre-warp the
face UV grid before sphere projection to keep quads square. Our `cubed_sphere_distortion_factor`
is a post-hoc correction; pre-distortion would produce better-looking blocks.

---

## 2. Shell-Based Layering (Depth Distortion)

**Problem**: On a sphere, identical block layers at different radii have different
physical sizes — blocks near center are thin, blocks near surface are wide.

**Solution**: Organize layers into **shells**. Each outer shell doubles the
resolution per axis (quadruples block count per layer). Within a shell, block
edges align; at shell boundaries, outer-shell blocks are 1/4 the size of
inner-shell blocks. This keeps block width roughly constant with depth.

Alternative (used by PlanetSmith): limit player to a single shell (min/max
build height). Viable for large planets where shells are exponentially huge.

**VOXOV relevance**: We currently treat the planet as a heightfield surface
with no volumetric blocks below the terrain. If we later add Minecraft-style
digging/building, we need this shell system. Our `PlanetDefinition.voxel_size`
is 1.0 — but at different radii on the sphere, voxel columns have different
physical widths. Shells solve this.

---

## 3. Hierarchical Block Addressing

```
BlockAddress {
    sectorIndex  // 0-5, dominant axis + sign
    shellIndex   // 0..∞, by radial distance loop
    chunkIndex   // int3, reverse-projected onto cube face
    blockIndex   // int3 [0-15]³
}
```

**Sector**: `max_abs(coord)` picks axis, sign picks Pos/Neg face.
**Shell**: loop over shell radii (shells have different heights).
**Chunk/Block**: reverse-project world_pos onto cube face → (u,v) in [-1,1]
→ remap to [0, N-1] where N = # horizontal chunks in current shell.
Y: inverse-lerp radius between shell min/max → multiply by # vertical chunks.

---

## 4. Cross-Sector Neighbor Lookup

12 edge pairings between 6 cube faces. Each pairing has axis mismatches:
- u-axis of one face may map to v-axis of adjacent face
- Axes may be flipped (positive→negative)

Bowerbyte defines a consistent **cube net** (unfolded cube diagram) to
determine how faces are glued. Neighbor lookup at sector boundaries requires
swapping/flipping local (u,v) indices based on this net.

**VOXOV relevance**: Our `neighbor_chunk_id()` is dead code that clamps at
face boundaries. We need to implement cross-face neighbor lookup using a
consistent cube net. Bowerbyte's cube net diagram provides a concrete reference.

---

## 5. Vertical Shell Boundary Neighbors

At a shell boundary, a block at the top of shell N maps to **4 blocks** at
the bottom of shell N+1 (2×2 since resolution doubles per axis). Inverse:
those 4 blocks share one downward neighbor.

This breaks the 1:1 neighbor assumption. Code must handle `vector<neighbor>`
returns for vertical lookups across shell boundaries.

---

## 6. 3D Noise on Sphere (Seamless Terrain)

Instead of mapping 2D noise to a sphere (which introduces seams and distortion),
sample a **3D noise function on the sphere surface**:

- Sample 3D noise at `normalize(world_pos)` on the unit sphere
- Different planet sizes = sample at different radii in noise space
- Random seeds = translate the sphere in noise space
- No seams (3D noise is continuous everywhere)
- No distortion (not converting from 2D to 3D)

Biomes: arctic near poles (angular distance < threshold + noise). Forest elsewhere.

**VOXOV relevance**: Our `terrain_height_fn` uses 2D noise on face UV coordinates,
which will have seams at face boundaries. Switching to 3D noise on the sphere
surface eliminates seams. Implementation: `noise3d(normalize(world_pos) * frequency + seed_offset)`.

---

## 7. Player Gravity

- **Above surface**: constant gravity toward planet center (predictable gameplay)
- **Below surface**: gravity decreases toward 0 at center (equal pull from all directions)
- **Flying**: thruster (hold SPACE) counteracts gravity for orbital flight
- **Rotation**: smoothly interpolate player up-vector to align with negative gravity
  (do NOT snap each frame — causes stuttering near planet center)

**VOXOV relevance**: Our `set_planet_surface_collider` provides radial gravity.
Player rotation currently uses hardcoded `(0,1,0)` up vector (broken). Need:
1. Replace camera up with `collision_world.planet_up_at(player_pos)`
2. Smoothly interpolate player rotation to match local up

---

## 8. Block Structure Placement (Not Box-Based)

Instead of pasting a 3D box of blocks (which fails at sector corners where 3
blocks meet instead of 4, and at shell boundaries), store structures as
**relative directions from an origin block**. Place the origin, then navigate
outward using neighbor-following. Works everywhere.

**VOXOV relevance**: If we add building mechanics later, use this approach
instead of box paste.

---

## 9. Chunking Strategy

- Chunks are fixed 16×16×16 regardless of shell
- Enables uniform batch processing (meshing, physics)
- Shells near planet center are too small for chunks → hollow core
- Sectors subdivided into shells, shells subdivided into chunks

---

## 10. Key Differences: Bowerbyte vs VOXOV Current Architecture

| Concern | Bowerbyte | VOXOV (current) |
|---------|-----------|-----------------|
| Block size at depth | Shell system (quadrupling) | Heightfield only (no subsurface blocks) |
| Terrain noise | 3D noise on sphere (seamless) | 2D noise on face UV (face seams) |
| Player gravity | Aligned to local up, smooth rotation | Hardcoded (0,1,0) up (broken on sphere) |
| Cross-face neighbors | Consistent cube net | Dead code (clamps at boundary) |
| Quad distortion | Pre-distortion + normalization | Post-hoc distortion factor |
| Surface | Cubic voxels with textures | Greedy-meshed heightfield (no textures) |

## 11. Immediate VOXOV Improvements (Prioritized)

1. **3D noise for terrain**: Replace `terrain_height_fn` 2D face-UV noise with
   3D noise sampled at `normalize(world_pos)`. Eliminates face-boundary seams.
   Single most impactful fix.

2. **Player up-vector**: Use `collision_world.planet_up_at(player_pos)` for
   camera and player rotation. Fixes movement orientation on sphere.

3. **Pre-distorted cube-sphere**: Investigate pre-warping face UV grid before
   sphere projection to keep quads square. Improves visual quality at face edges.

4. **Cross-face neighbor lookup**: Implement `neighbor_chunk_id` with a
   consistent cube net like Bowerbyte's. Required for seamless terrain meshing
   across face boundaries.

5. **Shell system** (future): Required for subsurface blocks (digging/building).
