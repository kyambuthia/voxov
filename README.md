# VOXOV

VOXOV is a C++23 voxel game prototype with multiplayer, physics, and multiple runtime targets built from one repository.

The current project shape is:

- Linux and Windows boot through the shared desktop `GameRuntime` adapter.
- Android packages the same `GameRuntime`, gameplay, renderer, and ENet protocol in a native APK.
- Web runs the same runtime in WebAssembly and joins native ENet servers through the included WebSocket-to-UDP gateway.
- A standalone authoritative server binary ships as `voxov_server`.

## Install

Release artifacts are published on GitHub Releases:

- https://github.com/kyambuthia/voxov/releases

Published bundles currently include:

- Linux: `VOXOV-<tag>-linux-x86_64.tar.gz` (client + dedicated server)
- Windows: `VOXOV-<tag>-windows-x86_64.zip` (client + dedicated server)
- Android: `VOXOV-<tag>-android-universal.apk` (arm64-v8a, armeabi-v7a, and x86_64)
- Web: `voxov_web.html`, `voxov_web.js`, `voxov_web.wasm`, and `voxov_web.data`
- Integrity: `SHA256SUMS.txt`

## Build From Source

Clone with submodules:

```bash
git clone --recurse-submodules github.com/kyambuthia/voxov.git
cd voxov
```

Desktop build:

```bash
cmake -S . -B build/desktop/main -DVOXOV_BUILD_TESTS=ON
cmake --build build/desktop/main --parallel
```

Run:

```bash
./build/desktop/main/bin/voxov
```

Dedicated server:

```bash
./build/desktop/main/bin/voxov_server --port 7777
```

Tests:

```bash
ctest --test-dir build/desktop/main --output-on-failure
```

See [`docs/CROSS_PLATFORM_PLAY.md`](docs/CROSS_PLATFORM_PLAY.md) for Android,
WebAssembly, dedicated-server, and cross-platform multiplayer commands.
See [`docs/SCALABILITY_ROADMAP.md`](docs/SCALABILITY_ROADMAP.md) for the
planet-to-galaxy architecture boundaries and prioritized engineering work.

## Architecture Snapshot

- `src/game/main_sokol.cpp` is the desktop entry point and boots the game through `GameRuntime` plus desktop input/platform adapters.
- `src/game/server_main.cpp` is the standalone dedicated server entry point.
- `src/game/game_runtime.cpp` is the runtime-facing shell currently wrapping the transitional desktop `Engine`.
- `src/engine/*` still owns most desktop simulation, rendering, networking, UI, and gameplay orchestration while the runtime extraction continues.
- Networking is now split into protocol (`src/engine_net_proto/*`), ENet transport (`src/engine_net/*`), LAN discovery, and server-session layers.
- `src/game/android_main.cpp` and `src/game/web_main.cpp` provide platform entry points around the shared `GameRuntime`.
- Core third-party dependencies are Sokol, ENet, GLM, fmt, spdlog, cgltf, and stb.

The codebase is designed reasonably well for a fast-moving prototype: the renderer targets desktop APIs and GLES3/WebGL2, the build graph is split into engine modules, the dedicated server is separated into its own binary, and CI validates packaged artifacts. The main design debt is the large `Engine` orchestration layer that still owns too many responsibilities behind the runtime seam.

The current refactor direction is to deepen the `GameRuntime` contract and continue shrinking `Engine` while keeping all platform adapters on the same simulation and networking interfaces.

## Controls

- `WASD`: move; mouse: look; `Space`: jump; `Shift`: sprint.
- Left mouse breaks a targeted block; right mouse places the selected block.
- `1`-`7`: choose the block to place (stone, dirt, grass, sand, planks, brick,
  snow).
- `V`: toggle first/third-person camera; mouse wheel zooms the third-person
  orbit.
- Spaceship: walk up to the ship parked near spawn and press `E` to board.
  `W`/`S` thrust and brake, mouse steers, `A`/`D` roll, `Space`/`Ctrl` lift
  and descend, `Shift` boost, `E` to exit once landed. Fly to Aster or Luna
  (markers show their range); entering a body's sphere of influence hands
  the ship over to that body, and you can land and build there.
- `F6`: sky navigation; point at a destination, press `Enter` to lock it,
  then hold `W` to engage flight assist. Aboard the ship, assist steers and
  brakes with its thrusters, matches the destination's orbital motion, and
  keeps the course locked through the body-frame transfer until touchdown.
- `Esc`: menu; `F11`: fullscreen; `F12`: screenshot.

The in-game expedition panel guides the first journey from Voxov to Aster.

## Assets

- Ships load from `assets/models/ships/`. Shrike is the default; run
  `VOXOV_SHIP=courier ./build/desktop/main/bin/voxov` to select Courier.
  Missing or malformed models use the primitive ship fallback.
- Ship voxels are drawn at 0.25 m (`kShipVoxelSizeMeters` in
  `src/engine_gameplay/vehicles/spaceship.hpp`), so Shrike is about
  12.75 x 2.5 x 15 m and Courier 4.75 x 2.5 x 7 m. To change the scale,
  edit that constant and `VOXEL_SIZE_METERS` in the generator together.
- `python3 tools/generate_ship_voxels.py` regenerates the `.vox` models and
  mount-point JSON. `python3 tools/preview_ship_voxels.py` creates review
  images (requires Pillow). The current integration renders the hull and
  scales boarding bounds, exit placement, landing clearance, and the chase
  camera. Walkable interiors, per-voxel ship collision, and mount-point
  interactions are not implemented yet.
- Ship reference art and its provenance live in
  `assets/references/spaceships/<ship>/SOURCE.md`. It is art direction only;
  the generator is the source of truth for the models.
- UI sounds live under `assets/audio/ui/`.
- Audio provenance and licensing are documented in `assets/audio/ui/README.md`.

## Project status

VOXOV is an actively developed prototype. Current release notes and playable
artifacts are available on the linked GitHub Releases page.
