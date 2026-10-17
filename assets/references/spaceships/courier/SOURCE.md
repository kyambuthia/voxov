# Spaceship Reference Art Source

- Assets: `courier_3q.png`, `courier_ortho_side.png`, `courier_ortho_top.png`,
  `courier_ortho_front.png`
- Origin: generated locally on 2026-10-11 with the Codex CLI's
  `image_gen__imagegen` tool (`codex exec`, model `gpt-5.6-luna`).
- The three orthographic views were generated with `courier_3q.png` passed as
  `referenced_image_paths` so all four show the same craft.

Notes:

- These are **art direction, not geometry**. The generator draws something that
  looks voxel but is not on a strict lattice: cube sizes drift, some edges are
  bevelled, and a stated voxel count is not honoured (the first test asked for
  12 voxels long and got about 20). Read proportion, massing and palette off
  them; never try to derive voxel coordinates from them.
- The shipped model is authored as voxel data by
  `tools/generate_ship_voxels.py`, which is the single source of truth for
  `assets/models/ships/courier.vox`. Editing the art here does not change the
  model.
- Prompts used are recorded in the commit that added these files. The style
  preamble that produced a usable result was: strict isometric or orthographic
  view, MagicaVoxel style, flat shaded solid colour cubes, visible voxel
  facets, hard edges, no textures, no gradients, no bevels, plain flat white
  background, no ground plane, no shadow, no text.
