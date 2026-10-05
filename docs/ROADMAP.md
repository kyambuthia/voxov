# VOXOV improvement roadmap

Branch: `improve/quality-roadmap`. Each item lands as its own commit with
`voxov_tests` passing and an in-game screenshot check (`VOXOV_AUTO_SCREENSHOT`)
for anything visual.

## Where the game is today

- Playable loop: walk a compact voxel planet, break/place blocks, follow the
  four-step Aster expedition, fly between bodies with sky navigation, LAN
  multiplayer through the authoritative server.
- New in this branch: third-person orbit camera (V), blocky explorer avatar.
- Main debts found while surveying:
  - `Engine::tick` is ~1,400 lines that mix input toggles, simulation, block
    editing, streaming, solar system, render prep, and profiling.
  - Debug-only `stderr` spam ships in release builds (per-frame `Vertex0 ...
    CLIPPED`, first draw calls, chunk generation, mesh face counts, LOD).
  - F5 shells out to `./auto_analyze.sh` with `std::system` in every desktop
    build. That's a developer tool wired into the player binary.
  - Block editing:
    - Placement falls back to "above the hit block" whenever the ray crosses
      a chunk boundary, because the face normal is only derived inside a single
      chunk.
    - Nothing stops the player from placing a block inside their own body.
    - Edits on a chunk border don't rebuild the neighbour chunk, whose
      cross-chunk culled faces leave a hole.
    - Every edit writes the whole save file synchronously on the frame
      thread.
    - Placement is hard-coded to stone.
  - In third person the pick ray starts at the orbit camera, so reach is
    measured from the camera and blocks behind the avatar can be targeted.

## Phase 1: correctness and hygiene (low risk, high value)

1. **Release-clean logging.** Route the diagnostic `fprintf(stderr, ...)`
   calls through `spdlog::debug` (or remove them) so normal runs log only
   meaningful events.
2. **Gate developer hooks.** Run the F5 auto-analysis script only when
   `VOXOV_DEV_TOOLS` is set. Replace `std::system("mkdir -p")` with
   `std::filesystem`.
3. **Block-edit correctness.**
   - Place into the last empty cell the pick ray passed through. That's exact
     across chunk and sector boundaries.
   - Reject placements that overlap the player's capsule.
   - Measure reach from the player's eye, not the orbit camera.
   - Rebuild neighbour chunk meshes when an edited block sits on a chunk
     face.
   - Factor the duplicated "invalidate chunk mesh" code into one helper.
4. **Debounced persistence.** Mark the save dirty on edits and flush at most
   every few seconds, plus on shutdown and session changes.

## Phase 2: structure

5. **Split `Engine::tick` into named phases**, as private methods with the
   same behaviour: session/input toggles, sky navigation and flight, player
   simulation, camera, block interaction, terrain streaming, solar system and
   frames, render preparation, and profiling. This turns the 1,400-line
   function into a readable orchestrator and makes the following features
   cheap to add.

## Phase 3: gameplay

6. **Block palette / hotbar.** Number keys 1-6 pick the material to place
   (stone, dirt, grass, sand, wood, glass/ice, depending on what
   `VoxelMaterial` supports). The selected material shows in the HUD, and
   persistence already stores the material per edit.
7. **Target block highlight.** A thin outline cube around the targeted block,
   so breaking and placing reads clearly in both camera modes.
8. **Remote players as animated explorers.** Multiplayer peers use the same
   blocky avatar, oriented to their network heading, with walk/idle chosen
   from replicated velocity.

## Phase 4: stretch

9. Touch and gamepad binding for the camera toggle (Android/web adapters).
10. Camera collision smoothing when the obstruction clears (ease back out
    rather than snapping).

## Out of scope for this pass

The protocol, server authority, and galaxy-scale work in
`docs/SCALABILITY_ROADMAP.md` keeps its own sequence. Nothing here changes the
wire format.
