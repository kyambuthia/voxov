# Shrike Reference Art Source

The Shrike is the predator counterpart to the Courier: manta-winged, wider
than it is long, twin engine pods flanking a low central fuselage. Named for
the bird - small, swept, and a predator out of proportion to its size.

- Assets: `shrike_3q.png`, `shrike_ortho_side.png`, `shrike_ortho_top.png`,
  `shrike_ortho_front.png`
- Origin: generated locally on 2026-10-11 with the Codex CLI's
  `image_gen__imagegen` tool (`codex exec`, model `gpt-5.6-luna`).
- The three orthographic views were generated with `shrike_3q.png` passed as
  `referenced_image_paths` so all four show the same craft.

Notes:

- These are **art direction, not geometry**. The generator draws something
  that looks voxel but is not on a strict lattice: cube sizes drift, some
  edges are bevelled, and a stated voxel count is not honoured. Read
  proportion, massing and palette off them; never derive voxel coordinates.
- The shipped model is authored as voxel data by
  `tools/generate_ship_voxels.py`, which is the single source of truth for
  `assets/models/ships/shrike.vox` (51 x 10 x 60 voxels, drawn in game at
  0.25 m per voxel: 12.75 x 2.5 x 15 m). Editing the art here changes nothing.
- Palette: bone white hull, dark graphite panels, pale cyan canopy, orange
  thrusters. Orange is load-bearing - `SpaceshipState::thrust_level` drives
  the exhaust glow off it.
