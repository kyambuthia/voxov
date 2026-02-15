## VOXOV
Voxel exploration engine foundation (C++23), renderer-agnostic gameplay path, Vulkan + OpenGL backends.

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
- `RMB` hold or toggle look mode + pointer lock
- `W/A/S/D` move
- `Space` jump
- `Shift` sprint
- `Q/E` camera distance
- `--devhud` enable gameplay/network instrumentation overlay + structured logs
- `--noclip` debug-only movement mode (comparison tool; default off)

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
