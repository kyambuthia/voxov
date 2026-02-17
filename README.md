## VOXOV
Voxel exploration engine foundation (C++23), renderer-agnostic gameplay path, Vulkan + OpenGL backends.

## Status
`WIP` (work in progress). This project is actively evolving and APIs, runtime behavior, and platform support can change between commits.

Current notes:
- Vulkan path is under active stabilization (validation-clean sync and swapchain behavior still being improved).
- Some CPUs may require disabling aggressive Jolt SIMD options (e.g. AVX2/F16C/FMADD/LZCNT) to avoid illegal-instruction startup failures.

## Current Showcase
Latest screenshots:

![VOXOV Screenshot 1](docs/media/voxov_state_01.png)
![VOXOV Screenshot 2](docs/media/voxov_state_02.png)

Latest gameplay clip (GIF from latest screencast):

![VOXOV Gameplay GIF](docs/media/voxov_state.gif)

## Prereqs
- `cmake>=3.16`
- C++23 compiler (`clang++`/`g++`/MSVC)
- Vulkan SDK (`vulkan`, `glslc`)
- OpenGL dev libs
- Submodules: `git submodule update --init --recursive`

## Build Matrix

Linux/macOS (desktop):
```bash
cmake -S . -B build -DVOXOV_BUILD_TESTS=ON
cmake --build build -j
```

Windows (desktop, VS generator):
```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DVOXOV_BUILD_TESTS=ON
cmake --build build --config Release
```

Android (backend scaffold compiles; full app packaging pending):
```bash
cmake -S . -B build-android \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=24 \
  -DVOXOV_ENABLE_ANDROID_BACKEND=ON
cmake --build build-android -j
```

Web/Emscripten (backend scaffold compiles; browser runtime integration pending):
```bash
emcmake cmake -S . -B build-web -DVOXOV_ENABLE_WEB_BACKEND=ON
cmake --build build-web -j
```

## Rebuilding Clean
Linux/macOS:
```bash
./scripts/clean_build.sh
```

Windows (PowerShell):
```powershell
.\scripts\clean_build.ps1
```

Windows (Batch):
```bat
scripts\clean_build.bat
```

Android:
```bash
./scripts/clean_android.sh
```

Web (Emscripten):
```bash
./scripts/clean_web.sh
```

## Run (desktop)
Vulkan:
```bash
./build/bin/voxov --renderer vulkan
```

OpenGL:
```bash
./build/bin/voxov --renderer gl
```

Server:
```bash
./build/bin/voxov --server
./build/bin/voxov --headless-server
```
Server port override:
```bash
./build/bin/voxov --headless-server --port 7777
```

## Tests
```bash
ctest --test-dir build --output-on-failure
```

## Controls (desktop)
- Mouse controls third-person camera orbit (GTA-style while focused)
- `W/A/S/D` move
- `Space` jump
- `Shift` sprint
- `Q/E` camera distance
- `Esc` GUI menu (W/S or arrows + Enter)
- `--devhud` enable gameplay/network instrumentation overlay + structured logs
- `--noclip` debug-only movement mode (comparison tool; default off)
- `--splitscreen` local 2-player split view (P1: mouse+WASD, P2: IJKL + arrow keys, Ctrl jump)
- `F1` toggle collision debug draw
- `F2` toggle debug xray mode (depth-disabled debug draw)
- `F3` toggle collision-only debug primitives
- `F4` freeze/unfreeze current debug frame

## Visual Debug Flags
```bash
./build/bin/voxov --debug-collision
./build/bin/voxov --debug-xray
./build/bin/voxov --debug-collision-only
./build/bin/voxov --debug-freeze
```

## Dear ImGui Integration
- Dear ImGui is integrated and currently rendered in the OpenGL backend (`--renderer gl`).
- The in-game "VOXOV Debug" panel shows runtime metrics and debug hotkey reminders.

## LAN Multiplayer (2 PCs, same Wi-Fi)
PC A (server):
```bash
./build/bin/voxov --headless-server --port 7777
```

PC B (client 1):
```bash
./build/bin/voxov --renderer vulkan --connect <PC_A_LAN_IP> --port 7777 --devhud
```

PC C (client 2):
```bash
./build/bin/voxov --renderer gl --connect <PC_A_LAN_IP> --port 7777 --devhud
```

Validation checklist:
- both clients show `Assigned network player id=...` in logs
- devhud `REM` value is `>=1`
- moving on one client updates the other client’s remote capsule

## Asset Cooker
```bash
./build/bin/voxov_asset_cooker gltf assets/ship.glb build/ship.vasset
./build/bin/voxov_asset_cooker texture assets/albedo.ktx2 build/albedo.vtex
```

## Docs
- `docs/GENESIS_REFACTOR_PLAN.md`
- `docs/ARCHITECTURE.md`
- `docs/BUILD_PLATFORMS.md`
- `docs/EXTENDING.md`
- `docs/SETUP.md`
