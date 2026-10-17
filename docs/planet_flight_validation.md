# Planet flight and atmosphere

The playable system has one star and three landable bodies: Voxov, Luna, and
Aster. Travel remains local interplanetary flight; this pass does not add
another star system or a warp drive.

## Flight contracts

- Ship velocity is relative to the active body's translating frame. Transfers
  preserve solar position and velocity by adding the departing body's position
  and velocity and subtracting the destination's.
- Vacuum drag defaults to zero. Gravity still follows the existing inverse
  square law, with `mu = surface_gravity * body_radius^2`. The gameplay speed
  cap remains in effect.
- Integration substeps limit nominal travel between heightfield contacts to
  half a metre. This prevents a fast descent from skipping through small moons.
- Landing uses the incoming normal speed before cancelling ground penetration.
  Hard contacts rebound; soft contacts can park the ship. This is an arcade
  contact response, without hull damage or terrain-slope landing gear.
- Assisted approaches include the destination's relative orbital velocity.
  Once in the destination frame, braking range uses its sampled terrain height
  and the ship's hull clearance.
- Ship navigation uses F6, Enter to lock, and held W to engage guidance. A
  locked course survives entry into the destination's body frame and unlocks
  on touchdown. Guidance uses elapsed time for turning and signed thrust for
  immediate braking. Releasing W suspends assist. The player-only arrival
  shortcut is bypassed while piloting.

## Planet representations

Resident near chunks retain full voxel detail. Their existing mixed-stride LOD
is disabled because that path does not stitch neighboring resolutions.

Complete globes use 8, 16, 32, or 64 subdivisions per cube face, selected from
projected diameter, viewport height, and vertical field of view. Refinement and
coarsening have separate thresholds. Every globe level has a complete,
watertight face grid; tests cover all paired face edges at all four levels.
Level changes are discrete, without geometric morphing.

Each destination globe uses its own terrain runtime's deterministic height
sampler, matching the terrain used after transfer. Geometry is cached per body
and resolution; orbital motion changes only the double-precision mesh origin.
Texture coordinates use the surface block resolution, keeping texel scale
stable across globe LOD changes. The active globe remains slightly inset as a
fallback behind streamed chunks. Global previews represent generated terrain;
individual block edits remain in the detailed chunk layer.

## Sky and atmosphere

The background is one triangle per view with depth writes disabled. An
eight-sample view-ray integration produces stylized atmosphere shading, with
angular block cells, a square sun, stars, and clustered clouds on a shell.
Ray directions use the camera rotation, so rolling and radial traversal retain
a world-anchored sky. A clip depth inside the projection avoids precision loss
when sky navigation extends the far plane.

The compact atmosphere profile uses artistic scattering coefficients scaled
with atmosphere height, rather than unscaled Earth coefficients on a 64 m
planet. Height and density profiles scale on body transfer, with a minimum
height that keeps the cloud shell above the terrain envelope. CPU scattering
clips view rays at ground and uses an analytic ground-shadow test. Terrain
transmittance integrates only the finite atmosphere segment, preventing haze
from extending across empty space. The wireframe atmosphere preview is off by
default.

## Verification

```sh
cmake -S . -B build/desktop/main -DVOXOV_BUILD_TESTS=ON
cmake --build build/desktop/main --parallel 3
ctest --test-dir build/desktop/main --output-on-failure
```

Tests explicitly keep `assert` enabled in Release. Coverage includes a complete
circular orbit with bounded radius and energy error, vacuum coasting, fast
moon contact, soft touchdown, LOD projection and hysteresis, globe winding and
seams, atmosphere scale invariance, night-side shadow, and vacuum visibility.

The shader build targets GLSL 410, GLSL ES 300, HLSL 5, and Metal. Runtime visual
checks can use `VOXOV_CAPTURE_ORBIT=1` or `VOXOV_CAPTURE_FLIGHT=1`, together with
`VOXOV_AUTO_SCREENSHOT` and `VOXOV_AUTO_SCREENSHOT_FRAME`. These observation
captures hold position while terrain settles. `VOXOV_CAPTURE_SHIP=3` exercises
the assisted journey to Aster. Use a copied executable in a temporary directory
to keep visual checks' save data separate from a player's save.

## Primary references

- [NASA: Flight to Orbit](https://www1.grc.nasa.gov/beginners-guide-to-aeronautics/flight-to-orbit/)
  explains the role of horizontal velocity in maintaining an orbit.
- [Bruneton: atmospheric scattering functions](https://ebruneton.github.io/precomputed_atmospheric_scattering/atmosphere/functions.glsl.html)
  documents atmosphere boundaries, ground occlusion, and transmittance. Voxov
  uses a much smaller artistic ray integration, without precomputed scattering
  tables or multiple scattering.
- Rendering follows the repository's vendored Sokol API documentation and its
  bufferless background-triangle example at revision
  `453c71214fbb55d782683d20ea7e6c07314e3e9b`. Projection uses GLM 1.0.2 at
  revision `e7714885929f1dcd2bdc2531cc99d3c1dc6daa18`.
