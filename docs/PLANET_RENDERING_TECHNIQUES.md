# Planet Rendering, Procedural Generation, Space Flight & Landing — Technical Reference

## 1. Cube-Sphere Parametrization (Cubemap Sphere)

**Source: Inigo Quilez — [iquilezles.org/articles/patchedsphere](https://iquilezles.org/articles/patchedsphere/)**

Take a unit cube centered at origin, generate vertices on each face, normalize each vertex to unit length → sphere. Each of 6 faces has a natural rectangular (s,t) ∈ [0,1]² parametrization, avoiding polar singularities of lat/long spheres.

### Sphere Mapping (Face → Sphere)
For +Z face with params `(s,t)` in [0,1]:
```
x = 1 - 2s
y = 1 - 2t
k² = x² + y² + 1
q = (x, y, 1) / √(k²)
```

### Analytical Tangent Basis (exact, per-pixel, no differencing)
```
u = ∂q/∂x = (1 + y², -xy, -x) · k⁻³
v = ∂q/∂y = (-xy, 1 + x², -y) · k⁻³
```
Simplified (direction only):
```
u = (1 + y², -xy, -x)
v = (-xy, 1 + x², -y)
n = u × v = (x, y, 1)  // points radially outward
```
Key properties: exact (not finite-difference), cheap (no trig), per-pixel capable, low parameter-space distortion for texture/normal mapping.

### Inverse Mapping (Sphere → Face)
Ray from origin with direction q hits plane z=1 at:
```
s = 0.5 - 0.5 * qx/qz
t = 0.5 - 0.5 * qy/qz
```

### Practical Implications for Planet Rendering
- Each of 6 cube faces independently parametrized → natural quadtree subdivision per face
- No singularities at poles → uniform mesh quality across entire sphere
- Tangent basis enables correct normal mapping on spherical surface
- Works well with GPU cubemap hardware

---

## 2. Quadtree LOD on Cube-Sphere Faces

### Structure
Each of 6 cube-sphere faces holds an independent quadtree. Root node covers the entire face [(0,0), (1,1)] in (s,t) space. Nodes store:
- World-space bounds (AABB or sphere)
- LOD level (0 = root, increasing = finer)
- State: Empty, Stitched, Resident (loaded), Evicting
- Children: 4 quadrants (TL, TR, BL, BR)

### LOD Selection Metric
For each quadtree node, compute **screen-space error** ρ:
```
ρ = (node_world_diameter / distance_to_camera) * screen_height_pixels
```
Refine when `ρ > error_threshold_pixels` (typically 2-4 pixels). The error metric must account for:
- **Frustum culling**: reject nodes outside camera frustum
- **Horizon culling**: nodes on far side of planet (dot product of view vector and node center direction)
- **Occlusion culling** (optional): nodes behind foreground terrain

### LOD Hysteresis
Add hysteresis to avoid rapid LOD switching during camera dolly:
```
refine if ρ > threshold * 1.15
merge  if ρ < threshold * 0.85
```

### Boundary Stitching (Skirts / Degenerate Triangles)
Between adjacent LOD levels, gaps appear. Solutions:
- **Skirts**: extend each mesh chunk downward at edges by ~1-2% of chunk size. Simple, fill rate cost.
- **Stitching strips**: generate degenerate triangles at boundaries that match neighbor LOD. More triangles, zero fill waste.
- **Seamless clipmaps**: for heightfield approaches, clamp vertex heights at shared edges to coarser LOD values.
- GPU-friendly: Use vertex texture fetch with clamped LOD to guarantee matching samples across boundaries.

### Chunk Streaming
- **Generation budget**: limit new chunk generation per frame (e.g., 8 chunks/frame)
- **Max resident chunks**: cap at ~256 visible chunks
- **Priority queue**: sort pending chunks by screen-space error descending; generate high-error chunks first
- **Eviction**: unload chunks when screen-space error falls below threshold for N consecutive frames

---

## 3. Coordinate Systems & Floating Origin

### The Precision Problem
`float` (32-bit) has ~7 decimal digits of precision. At planetary scale:
- Planet diameter > 10,000 km → vertex positions lose sub-meter precision
- Camera at planetary surface coordinates like (10,000,000, 0, 0) → objects near camera jitter by centimeters/meters
- Physics collision breaks down at large coordinate magnitudes

### Camera-Relative Rendering (Floating Origin)
All rendering operates in a coordinate system centered on the camera:

```
struct CameraRelativeOrigin {
    vec3d world_origin;  // double precision
};

vec3 camera_relative_position(vec3d world_pos, CameraRelativeOrigin origin) {
    return vec3(world_pos - origin.world_origin);
}
```

Implementation rules:
1. **World state stored in `double`** (vec3d/glm::dvec3). All entity positions, chunk coordinates double precision.
2. **Per frame**: set `world_origin = camera.dvec3_position`. Convert all visible entities to camera-relative `float` for GPU.
3. **Physics**: step physics in a local coordinate system. Re-center when the simulation body moves beyond a threshold (e.g., 10 km from origin).
4. **Continuous floating origin**: instead of periodic snapping, continuously transform the world each frame. Avoids pop.

### Multi-Origin Systems
For multiplayer or distant objects:
- Large static bodies (planets, moons): use local origins (planet center)
- Transition between origins during SOI (sphere of influence) changes
- Store origin hierarchy: star system → planet → local surface

### Rotation Floating Origin
For planets where up-vector changes significantly over visible area:
- Rotate world so camera's local "up" aligns with world Y axis
- Avoids gimbal issues when gravity direction changes rapidly

---

## 4. Procedural Terrain Generation on Planet Scale

### Heightfield Approach (Cube-Sphere Face)
Each face treated as a planar heightfield projected onto the sphere:

```
vec3 face_to_world_sphere(float s, float t, float height, PlanetDefinition def) {
    vec3 dir = normalize(face_uv_to_direction(s, t, def.face));
    return def.center + dir * (def.radius + height);
}
```

Height function per face sample:
```
float terrain_height(vec2 uv, uint64_t seed, PlanetFace face) {
    // Multi-octave layered noise
    float h = 0;
    h += continental_noise(uv, 1024.0, seed) * 4000.0;  // continents
    h += mountain_noise(uv, 256.0, seed) * 1500.0;      // mountain ranges
    h += hill_noise(uv, 64.0, seed) * 200.0;             // hills
    h += detail_noise(uv, 16.0, seed) * 30.0;            // surface detail
    h += ridge_noise(uv, 128.0, seed) * 800.0;           // ridge lines
    h -= ocean_mask * 3000.0;                             // ocean basins
    return h;
}
```

### Domain Warping for Organic Terrain
**Source: Inigo Quilez — [iquilezles.org/articles/warp](https://iquilezles.org/articles/warp/)**

Feed noise into itself: `f(p + fbm(p + fbm(p)))`. Each warp layer adds organic complexity without changing base frequency range. Implementation per planet sample:
```
float h = base_noise(uv, seed);
h += fbm(uv + fbm(uv + fbm(uv, ...), seed2), seed3) * amplitude;
```
Produces fractal-like terrain with ridge networks, eroded-looking valleys, and naturalistic mountain chains.

### Fast Floating-Point Quadtrees for Procedural Content
**Source: Seed of Andromeda (archived blog posts)**

- Quadtree nodes addressed by Morton codes (interleaved bit representation of 2D coordinates). Enables O(1) neighbor lookup.
- Use `uint64_t` locational codes — integer arithmetic for parent/child/neighbor operations without floating point.
- Storage: hash map keyed by `(face_index << 62) | morton_code`. Avoids pointer-heavy tree structure.
- Deterministic generation: hash `(morton_code, world_seed, face)` → random seed for noise functions. Zero storage for unmodified terrain.

### Voxel Planet Approach
For voxel-based planets (like VOXOV):
- Each quadtree leaf maps to a `VoxelChunk` (e.g., 64³ voxels per chunk)
- Voxel terrain heightfield sampled at chunk resolution
- Cube-sphere vertex remapping: each voxel column's world-space position projected onto sphere surface
- Radial up-vector per voxel column: `radial_up = normalize(world_pos - planet_center)`

### Material Assignment
```
voxel_material = {
    if (voxel_radial_height <= 0)              return Air;
    if (voxel_radial_height <= surface_height) {
        if (depth_from_surface <= 1)            return Grass;   // top layer
        else if (depth_from_surface <= 4)       return Dirt;    // subsurface
        else                                     return Stone;  // deep
    }
    return Air;
}
```

---

## 5. Mesh Generation from Voxel Chunks on Sphere

### Greedy Meshing on Cube-Sphere
Standard greedy meshing operates in axis-aligned grid space. For sphere projection:
1. Generate voxel chunk in local face coordinates (x, y, z local to face plane)
2. Run greedy meshing to produce face-quads in local space
3. Remap each quad vertex from local face space → world sphere position using `face_to_world_sphere()`
4. Compute per-vertex normal from gradient of world sphere position

### Per-Voxel Tangent Frame
For correct lighting on curved surface:
- Each voxel has a local "up" vector = `normalize(world_voxel_pos - planet_center)` (radial)
- Tangent and bitangent from analytical derivatives (see section 1)
- Vertex normal = weighted average of adjacent face normals after sphere projection

---

## 6. Space-to-Surface Transition (Atmospheric Entry)

### Coordinate Zones
```
Zone  Space      → distance > atmosphere_radius     : planet as textured sphere
Zone  Descending → entering atmosphere              : fade in terrain, fade out proxy sphere
Zone  Surface    → distance < max_terrain_distance  : full terrain LOD
Zone  Ascending  → leaving atmosphere               : reverse of descending
```

### Transition Details
- **Space render**: single sphere mesh with:
  - Color texture (low-res procedural baked to cubemap)
  - Normal map (from terrain heightfield at coarse resolution)
  - Atmosphere glow shader
- **Fade**: interpolate opacity between space sphere and terrain chunks during transition. Alpha = `smoothstep(atmo_radius * 0.9, atmo_radius * 1.1, distance)`.
- **Freeze physics**: during transition, freeze rigidbody velocities to avoid jitter from coordinate system change
- **UI effects**: atmospheric heating effect (plasma glow), camera shake, sound transition

### Rendering in Space
- Render planet as impostor sphere when `distance > LOD_max_range`
- Cube-sphere quadtree starts loading when `distance < LOD_activation_distance`
- Prioritize visible face(s) — the face(s) facing the camera (typically 1-3 of 6 faces)

---

## 7. Physics & Collision at Planetary Scale

### Radial Gravity
```
vec3 gravity_acceleration_at(vec3 world_pos, vec3 planet_center, float GM) {
    vec3 r = world_pos - planet_center;
    float dist = length(r);
    return -normalize(r) * (GM / (dist * dist));
}
```

### Planet Surface Collision
Two approaches:

**A. Heightfield Collision** (fast, approximate):
- Project entity position to planet surface: `radial_distance = length(pos - center)`
- Sample terrain height at angular coordinates: `surface_height = terrain_height(azimuth, elevation)`
- Collision when: `radial_distance - planet_radius < surface_height + entity_radius`
- Contact normal = radial direction `normalize(pos - center)`

**B. Voxel Collision** (exact, expensive):
- For nearby voxel chunks, build collision volume
- Raycast against voxel grid for foot placement / projectile hits
- Capsule sweep test for player movement
- Spatial hash or octree for broadphase across quadtree chunks

### Floating Origin for Physics
- Physics engine steps in local coordinate space
- Re-center physics world when camera moves beyond threshold (e.g., 5 km from physics origin)
- Teleport all rigid bodies by the same offset during recentering
- Maintain inertia (velocity is invariant under translation)

---

## 8. Orbital Mechanics & Space Flight

### Two-Body Keplerian Motion
```
// State: position r, velocity v relative to central body
// Compute orbital elements from state vectors
vec3 h = cross(r, v);           // specific angular momentum
vec3 e_vec = cross(v, h)/GM - normalize(r);  // eccentricity vector
float e = length(e_vec);        // eccentricity
float a = -GM / (2 * T - v²);   // semi-major axis (from vis-viva: v² = GM(2/r - 1/a))
```
- Elliptical orbit: `e < 1`. Circular: `e ≈ 0`.
- Hyperbolic: `e > 1` (escape trajectory).
- **Patched conics**: piecewise two-body approximations for inter-body transfers. Switch reference frame at SOI boundary.

### SOI (Sphere of Influence)
```
R_SOI = a * (m/M)^(2/5)
```
where a = semi-major axis, m = planet mass, M = star mass.

### Atmospheric Drag
```
F_drag = 0.5 * ρ * v² * C_d * A
```
where ρ = atmospheric density at altitude (exponential falloff), v = velocity relative to atmosphere, C_d = drag coefficient.

### Landing Sequence
1. De-orbit burn: lower periapsis into atmosphere
2. Aerobraking: use atmospheric drag to decelerate (if atmosphere present)
3. Powered descent: thrust against gravity to control descent rate
4. Surface relative navigation: switch to local terrain-following mode below ~10 km altitude
5. Touchdown: detect surface contact via heightfield collision, kill residual velocity

---

## 9. Precision Techniques for Large Worlds

### Double Precision Where It Matters
| Data | Type | Reason |
|------|------|--------|
| Entity world position | vec3d (double) | Prevents jitter at scale |
| Planet center | vec3d | Anchor for local coordinates |
| Camera world position | vec3d | Origin for camera-relative rendering |
| Vertex positions (GPU) | vec3 (float) | Already camera-relative |
| Chunk world coordinates | int32/int64 | Integer addressing → no precision loss |
| Noise evaluation inputs | float | Use cell-space trick (see below) |

### Cell-Space Noise Evaluation
**Source: Inigo Quilez — smooth voronoi article**
When evaluating noise/procedural functions at planet scale:
1. Compute double-precision world position
2. Extract `floor()` and `fract()` in double precision
3. Drop the integer part — all noise computation uses `fract()` values and cell-local coordinates
4. Remaining computation in float precision, safe because values are in [0,1] range
```
ivec2 cell = ivec2(floor(world_pos_double));
vec2  local = vec2(world_pos_double - vec2(cell));  // implicitly float
// local is in [0,1], all subsequent ops are float-safe
```

### Camera-Relative GPU Upload
Each frame, for every visible entity:
```
gpu_pos = vec3(entity.world_pos_dvec3 - camera_origin_dvec3);
```
Never accumulate world-space floats over multiple frames.

---

## 10. Atmospheric Rendering (Brief Overview)

### Rayleigh Scattering
- `β_R(λ) ∝ 1/λ⁴` — blue light scatters more → sky is blue, sunsets are red
- Precompute: optical depth integral along view rays via lookup table (Bruneton 2008 / Hillaire 2020)

### Mie Scattering
- `β_M ∝ 1/λ⁰` — wavelength-independent → haze, clouds, sun glow

### Practical for Voxel Engines
- Atmospheric color applied as post-process (fullscreen quad sampling precomputed LUT)
- Skybox: procedurally generated cubemap from atmospheric model
- Transition from space: atmosphere opacity fades in based on altitude

---

## 11. References & Further Reading

### Foundational Techniques
- **Cube-Sphere Parametrization**: Inigo Quilez, "Patched Sphere" — [iquilezles.org/articles/patchedsphere](https://iquilezles.org/articles/patchedsphere/)
- **Domain Warping**: Inigo Quilez, "Domain Warping" — [iquilezles.org/articles/warp](https://iquilezles.org/articles/warp/)
- **Smooth Voronoi / Planet-Safe Noise**: Inigo Quilez, "Smooth Voronoi" — [iquilezles.org/articles/smoothvoronoi](https://iquilezles.org/articles/smoothvoronoi/)
- **Floating Origin**: Dr. Chris Thorne, "The Relative Spacetime Project" — [floatingorigin.com](https://floatingorigin.com/)

### Planet Rendering Engines (Study Reference)
- **Outerra** (world-scale planet engine): cube-sphere quadtree, vector displacement terrain, atmospheric scattering, continuous LOD
- **Kerbal Space Program**: floating origin for solar system, patched conics orbital mechanics, PQS (Procedural Quadtree Sphere) terrain
- **Elite Dangerous**: galaxy-scale stellar forge procedural generation, cube-sphere planets, seamless space-to-surface
- **Star Citizen**: nested coordinate systems (zone system), 64-bit world positions, seamless transitions
- **No Man's Sky**: pure procedural generation from single seed, voxel-based terrain, marching cubes on sphere
- **Seed of Andromeda**: open-source voxel planet, fast floating-point quadtrees, procedural generation from seed

### Algorithms & Data Structures
- **CDLOD**: Filip Strugar, "Continuous Distance-Dependent Level of Detail for Rendering Heightmaps" (2010) — GPU-friendly LOD with morphing
- **GeoClipmapping**: Hoppe/Losasso, "Geometry Clipmaps: Terrain Rendering Using Nested Regular Grids" (2004)
- **Morton Codes**: Z-order curve for spatial indexing, enables efficient quadtree neighbor operations
- **Dual Contouring**: Ju et al., "Dual Contouring of Hermite Data" (2002) — smooth voxel surfaces (alternative to marching cubes)

### Atmospheric Scattering
- Bruneton/Neyret, "Precomputed Atmospheric Scattering" (EGSR 2008)
- Hillaire, "A Scalable and Production Ready Sky and Atmosphere Rendering Technique" (EGSR 2020)
- Yusov, "Outdoor Light Scattering Sample" (Intel, 2013) — GPU-friendly implementation

### Floating Origin & Large Worlds
- **Camera-Relative Rendering**: standard technique used in KSP, Elite, Star Citizen, Minecraft (since 1.14)
- **Physics recentering**: re-center rigid body simulation when origin exceeds threshold
- **Nested coordinate spaces**: for solar system scale, chain: galaxy → star system → planet → local surface
