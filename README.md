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

- Linux: `VOXOV-<tag>-linux-x86_64.tar.gz`
- Windows: `VOXOV-<tag>-windows-x86_64.zip`
- Android: `VOXOV-<tag>-android-arm64-v8a.apk`
- Web: `voxov_web.html`, `voxov_web.js`, `voxov_web.wasm`, and `voxov_web.data`

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

## Architecture Snapshot

- `src/game/main.cpp` is the desktop entry point and boots the game through `GameRuntime` plus desktop input/platform adapters.
- `src/game/server_main.cpp` is the standalone dedicated server entry point.
- `src/game/game_runtime.cpp` is the runtime-facing shell currently wrapping the transitional desktop `Engine`.
- `src/engine/*` still owns most desktop simulation, rendering, networking, UI, and gameplay orchestration while the runtime extraction continues.
- Networking is now split into protocol (`src/engine_net_proto/*`), ENet transport (`src/engine_net/*`), LAN discovery, and server-session layers.
- `src/game/android_main.cpp` and `src/game/web_main.cpp` provide platform entry points around the shared `GameRuntime`.
- Core third-party dependencies are GLFW, ENet, Dear ImGui, GLM, fmt, and spdlog.

The codebase is designed reasonably well for a fast-moving prototype: the renderer targets desktop APIs and GLES3/WebGL2, the build graph is split into engine modules, the dedicated server is separated into its own binary, and CI validates packaged artifacts. The main design debt is the large `Engine` orchestration layer that still owns too many responsibilities behind the runtime seam.

The current refactor direction is to deepen the `GameRuntime` contract and continue shrinking `Engine` while keeping all platform adapters on the same simulation and networking interfaces.

## Docs

- Canonical handbook: `docs/HANDBOOK.md`
- The handbook consolidates the current repository docs into one place and preserves the existing file contents verbatim.

## Assets

- UI sounds live under `assets/audio/ui/`.
- Audio provenance and licensing are documented in `assets/audio/ui/README.md`.

## Showcase

### Current Android Gameplay

![VOXOV Android Gameplay GIF](docs/media/voxov_android_gameplay.gif)

| Android Screenshot 1 | Android Screenshot 2 |
| --- | --- |
| ![VOXOV Android Screenshot 1](docs/media/voxov_android_gameplay_01.jpg) | ![VOXOV Android Screenshot 2](docs/media/voxov_android_gameplay_02.jpg) |

### Previous Showcase

![VOXOV Gameplay GIF](docs/media/voxov_state.gif)

| Screenshot 1 | Screenshot 2 |
| --- | --- |
| ![VOXOV Screenshot 1](docs/media/voxov_state_01.png) | ![VOXOV Screenshot 2](docs/media/voxov_state_02.png) |
