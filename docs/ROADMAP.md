# VOXOV improvement roadmap

Status on `trunk` (verified against `src/`). Each change should keep
`voxov_tests`, `voxov_event_bus_tests`, and `voxov_net_stress` passing.

## Where the game is today

- Playable loop: walk/sprint on spherical voxel planets (`Voxov`, `Luna`,
  `Aster`), break/place blocks with a 7-slot hotbar (`1`-`7`), follow the
  four-step Aster expedition, pilot `.vox`-hull spaceships (`Shrike` /
  `Courier`) between bodies with `F6` sky navigation and orbital frame
  transfers, and join LAN/dedicated-server multiplayer (`voxov_server`).
- Camera & avatars: first/third-person orbit camera (`V`), target block
  outline (`append_target_outline`), and animated blocky explorer avatars
  (`PlayerVisuals` / `RemoteAvatar`).
- Completed from the earlier quality pass:
  - **Release-clean logging**: diagnostic `stderr` spam removed; normal runs
    log via `spdlog`.
  - **No dev shell-outs**: removed the `F5` `auto_analyze.sh` `std::system`
    hook.
  - **Block-edit correctness**: eye-origin pick ray, placement into the last
    empty cell across chunk boundaries, player-capsule overlap rejection,
    neighbor chunk mesh invalidation (`invalidate_block_meshes`), and
    debounced persistence (`request_save` / `flush_pending_save`).
  - **`Engine::tick` phase split**: `Engine::tick` is now a ~120-line
    orchestrator calling named phase helpers (`apply_session_input`,
    `apply_sky_navigation_assist`, `update_camera`, `simulate_frame`,
    `stream_block_terrain`, `rebuild_wireframe_overlays`,
    `update_solar_system`, `update_coordinate_frames`,
    `append_celestial_meshes`, `append_remote_player_meshes`,
    `update_render_stats`, `build_render_context`).
  - **Block palette & target highlight**: 7-material `kBlockHotbar` (`1`-`7`)
    and `append_target_outline`.
  - **Remote avatars**: `append_remote_player_meshes` renders multiplayer peers
    with heading and velocity-driven animation.

## Remaining local & structural work

1. **Shrink `Engine` behind `GameRuntime`**: `src/engine/engine.cpp` is still
   ~3,400 lines and owns simulation, terrain streaming, ship control,
   networking glue, and render prep. Continue extracting subsystems into
   `engine_runtime`, `engine_world`, and `engine_gameplay`.
2. **Ship voxel fidelity**: `README.md` notes that walkable ship interiors,
   per-voxel ship collision, and mount-point interactions are not yet wired
   up.
3. **Mobile/web ergonomics & camera polish**: touch/gamepad binding for the
   camera toggle (`V`) and eased camera collision recovery when obstructions
   clear.
4. **Windows release DLLs (Issue #3)**: verify that CI/release Windows
   archives include `libgcc_s_seh-1.dll`, `libwinpthread-1.dll`, and
   `libstdc++-6.dll` (CMake `MINGW` post-build/install rules exist, but
   verify against the actual release toolchain).

## Scalability & multiplayer track

Protocol, server authority, async terrain jobs, and multi-system scaling
follow `docs/SCALABILITY_ROADMAP.md`.
