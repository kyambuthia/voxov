# Voxov Solar System Architecture — First Principles Design

## 1. Coordinate System — First Principles

**Problem**: At planetary scale (500m–2000km), float32 precision fails. At solar system scale (AU), even float64 fails.

**Solution**: Hierarchical coordinate frames.

```
Local frame:    Player position relative to chunk center (float32)
Planet frame:   Position on planet surface (float64, planet-centered)
Orbital frame:  Position relative to parent body (float64, orbital mechanics)
Galactic frame: Position in solar system (float64, heliocentric)
```

Each frame is offset from its parent. Camera-relative rendering uses the local frame for GPU precision.

## 2. LOD System — Distance-Based Chunk Resolution

**Principle**: Chunks farther from camera need fewer vertices.

**Implementation**:
- Each planet has a `ChunkLOD` level (0 = full res, 1 = half, 2 = quarter, etc.)
- LOD transitions based on screen-space error (pixels)
- Chunk mesh resolution: 16³ blocks, but vertex density varies with LOD
- Hysteresis prevents LOD popping (different thresholds for up/down transitions)

**Math**:
```
screen_error = (chunk_world_size / distance) * screen_height_pixels
if screen_error < threshold:  LOD++
if screen_error > threshold * hysteresis: LOD--
```

## 3. Atmosphere — Rayleigh + Mie Scattering

**From research** (Scratchapixel, Nishita 1993):

**Rayleigh Scattering** (air molecules):
- β_R(λ) = (8π³(n²-1)²) / (3Nλ⁴) × exp(-h/H_R)
- Scale height H_R = 8km (Earth), varies per planet
- Responsible for blue sky, red sunsets

**Mie Scattering** (aerosols):
- β_M(h) = β_M(0) × exp(-h/H_M)
- Scale height H_M = 1.2km (Earth)
- Forward scattering (g ≈ 0.76), responsible for haze

**Implementation** (GPU fragment shader):
- Ray march through atmosphere sphere
- Sample density at each step
- Compute optical depth to sun and camera
- Apply phase functions (Rayleigh + Mie)
- Composite with terrain color

## 4. Solar System — Orbital Mechanics

**Kepler's Laws**:
- Orbits are ellipses with parent body at one focus
- Semi-major axis (a), eccentricity (e), inclination (i)
- Mean anomaly → Eccentric anomaly → True anomaly

**Implementation**:
```
struct OrbitalElements {
    double semi_major_axis;  // AU or meters
    double eccentricity;     // 0 = circle, 0-1 = ellipse
    double inclination;      // radians
    double longitude_ascending_node;
    double argument_periapsis;
    double mean_anomaly_epoch;
    double orbital_period;   // seconds
};

Vec3 orbital_position(OrbitalElements oe, double time) {
    // Solve Kepler's equation: M = E - e*sin(E)
    // Compute true anomaly from eccentric anomaly
    // Convert to 3D position using orbital elements
}
```

## 5. Flight Vehicles — Physics Model

**Forces**:
- Thrust (engine force vector)
- Lift (wing surface, perpendicular to velocity)
- Drag (air resistance, opposite velocity)
- Gravity (toward parent body center)

**Equations of Motion**:
```
F_total = thrust + lift + drag + gravity
acceleration = F_total / mass
velocity += acceleration * dt
position += velocity * dt
```

**Atmospheric flight**: Lift depends on air density (altitude), angle of attack, wing area.

**Space flight**: No lift/drag, only thrust and gravity. Use patched conics for interplanetary transfers.

## 6. Rendering Pipeline

```
1. Update orbital positions (solar system bodies)
2. For each visible planet:
   a. Determine LOD level from camera distance
   b. Stream chunks at appropriate resolution
   c. Build meshes with greedy meshing
   d. Render atmosphere (ray march in fragment shader)
3. Render sun (billboard or corona shader)
4. Render stars (skybox)
5. Post-processing (tone mapping, bloom)
```

## 7. Implementation Order

1. **LOD System** — Foundation for all terrain rendering
2. **Atmosphere** — Visual quality, independent of LOD
3. **Solar System** — Orbital mechanics, body positions
4. **Flight Vehicles** — Physics simulation
5. **Inter-planetary Travel** — Coordinate frame transitions

## 8. File Organization

```
src/engine_world/
  planet_lod.*       — LOD selection and chunk resolution
  atmosphere.*       — Atmosphere rendering (Rayleigh + Mie)
  solar_system.*     — Orbital mechanics, body management
  
src/engine_physics/
  flight_vehicle.*   — Aerodynamic physics model
  orbital_mechanics.* — Kepler equation solver
  
src/engine_render/
  atmosphere_shader.* — GPU atmosphere rendering
```
