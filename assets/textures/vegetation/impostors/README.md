# Vegetation Octahedral-Impostor Source Atlases

Each atlas contains eight views of one individual plant in a 4 × 2 layout.
Frames are ordered left-to-right, then top-to-bottom, at yaw angles 0°, 45°,
90°, 135°, 180°, 225°, 270°, and 315°. The plants share a baseline and are
kept at a constant scale, allowing an octahedral-impostor renderer to select
or blend directional frames without a visual pop.

`raw/` retains the original image-generation output on a flat `#ff00ff`
chroma key. `transparent/` contains the soft-matted, despilled RGBA version
for runtime preprocessing and sampling. The transparent copies are trimmed by
at most two outer background pixels when necessary so every atlas divides
exactly into four columns and two rows; the raw masters are left unchanged.

Current species set:

- `temperate_grass_octa_4x2.png` — dense, low temperate grass tuft.
- `meadow_wildflowers_octa_4x2.png` — yellow-and-white meadow flower clump.
- `woodland_shrub_octa_4x2.png` — knee-high oval-leaf shrub.
- `forest_fern_octa_4x2.png` — low broad fern rosette.

The art is authored at source resolution. A runtime asset cooker should slice
each 4 × 2 source atlas into eight equally sized RGBA frames, generate alpha
mips with coverage preservation, and pack the result into the vegetation
impostor texture array. Do not use the older `voxel/` card textures as a
substitute: those are crossed-billboard assets, not directional views.
