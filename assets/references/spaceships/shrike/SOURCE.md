# Shrike Reference Art Source

The Shrike is the predator counterpart to the Courier: manta-winged, wider
than it is long, twin engine pods flanking a low central fuselage. Named for
the bird - small, swept, and a predator out of proportion to its size.

## Third-party concept art (`concept/`)

Supplied by the project owner on 2026-10-11, origin unrecorded. These are
**unlicensed third-party concept art held as private art direction only.**
Nothing in `concept/` ships in a build, and no part of it may be redistributed
or traced into a shipped asset.

- `concept_ar12_shuttle.jpg` - chunky magenta lifting-body shuttle marked
  "AR 12"; delta wings, twin tail fins, two large rear nozzles.
- `concept_glider_mechanoids.jpg` - sleek white predator captioned GLIDER,
  from "Мехaноиды: протокол войны" (Mechanoids: War Protocol); long blade
  wings, forward engine pods, folded landing claws.
- `concept_manta_sheet.jpg` - grayscale four-view sheet; swept manta form,
  huge raked wings, twin pods flanking a central canopy.

What was taken from them: silhouette and proportion only - the manta wing
sweep, the twin pods forward of the wing line, the low canopy between them,
the folded belly claws. Not their rendering, paint, palette or lettering.

## Generated views

Generated locally on 2026-10-11 with the Codex CLI's `image_gen__imagegen`
(`codex exec`, model `gpt-5.6-luna`), with the three files in `concept/`
passed as `referenced_image_paths`. The orthographic views additionally
reference `shrike_3q.png` so all four show the same craft.

- `shrike_3q.png`
- `shrike_ortho_side.png`, `shrike_ortho_top.png`, `shrike_ortho_front.png`

Notes:

- These are **art direction, not geometry**. The generator draws something
  that looks voxel but is not on a strict lattice: cube sizes drift, some
  edges are bevelled, and a stated voxel count is not honoured. Read
  proportion, massing and palette off them; never derive voxel coordinates.
- The shipped model is authored as voxel data by
  `tools/generate_ship_voxels.py`, which is the single source of truth for
  `assets/models/ships/shrike.vox`. Editing the art here changes nothing.
- Palette: bone white hull, dark graphite panels, pale cyan canopy, orange
  thrusters. Orange is load-bearing - `SpaceshipState::thrust_level` drives
  the exhaust glow off it.
