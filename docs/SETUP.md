# Setup Guide

## 1. Prerequisites

- CMake `>=3.16`
- C++23 compiler (`g++`, `clang++`, or MSVC)
- Vulkan SDK (must include `glslc`)
- OpenGL development libraries
- Git with submodule support

Linux (Debian/Ubuntu baseline):

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential cmake ninja-build \
  glslc libvulkan-dev libgl1-mesa-dev \
  libwayland-dev libx11-dev libxcursor-dev \
  libxi-dev libxinerama-dev libxkbcommon-dev libxrandr-dev
```

## 2. Clone

```bash
git clone --recurse-submodules https://github.com/kyambuthia/voxov.git
cd voxov
git submodule update --init --recursive
```

## 3. Configure + Build

Default desktop build:

```bash
cmake -S . -B build/desktop/main -DVOXOV_BUILD_TESTS=ON
cmake --build build/desktop/main --parallel
```

Useful configure flags:

- `-DVOXOV_BUILD_TESTS=ON`
- `-DVOXOV_ENABLE_VULKAN=OFF` if you want an OpenGL-only desktop build
- `-DVOXOV_ENABLE_XR=ON` to compile the XR scaffold
- `-DVOXOV_ENABLE_IOS_BACKEND=ON` to compile the iOS scaffold
- `-DVOXOV_ENABLE_CONSOLE_BACKEND=ON` to compile the console scaffold
- `-DVOXOV_BUILD_LEGACY_DEMOS=ON` to expose the legacy SDL/Vulkan sources in the build graph
- `-DUSE_AVX2=OFF -DUSE_F16C=OFF -DUSE_FMADD=OFF -DUSE_LZCNT=OFF` for older CPUs that crash with illegal-instruction.

Notes:

- Android and Web builds are selected by the toolchain (`ANDROID` or `EMSCRIPTEN`), not by a normal desktop configure flag.
- Desktop is the main integrated runtime. Android and Web use separate runtime paths today.

## 4. Run

Vulkan:

```bash
./build/desktop/main/bin/voxov --renderer vulkan
```

OpenGL:

```bash
./build/desktop/main/bin/voxov --renderer gl
```

Headless authoritative server:

```bash
./build/desktop/main/bin/voxov --headless-server --port 7777
```

## 5. Debug and Visual Debug

General gameplay debug:

```bash
./build/desktop/main/bin/voxov --devhud
./build/desktop/main/bin/voxov --devhud --noclip
```

Collision visual debugging:

```bash
./build/desktop/main/bin/voxov --debug-collision
./build/desktop/main/bin/voxov --debug-xray
./build/desktop/main/bin/voxov --debug-collision-only
./build/desktop/main/bin/voxov --debug-freeze
```

Hotkeys:

- `F1` toggle collision debug draw
- `F2` toggle xray debug draw
- `F3` toggle collision-only debug primitives
- `F4` freeze/unfreeze current debug frame

## 6. Tests

Build tests are only generated if the configure step used `-DVOXOV_BUILD_TESTS=ON`.

```bash
ctest --test-dir build/desktop/main --output-on-failure
```

## 7. Asset Cooker

```bash
./build/desktop/main/bin/voxov_asset_cooker gltf assets/ship.glb build/desktop/main/ship.vasset
./build/desktop/main/bin/voxov_asset_cooker texture assets/albedo.ktx2 build/desktop/main/albedo.vtex
```

## 8. Clean Rebuild

Linux/macOS:

```bash
./scripts/clean_build.sh
```

Windows PowerShell:

```powershell
.\scripts\clean_build.ps1
```

Windows Batch:

```bat
scripts\clean_build.bat
```

## 9. Releases

For release tags and downloadable binaries, see `docs/RELEASES.md`.

## 10. Roadmap

For current architectural priorities and platform sequencing, see `docs/ROADMAP.md`.

## 11. Android

For Android NDK/Android Studio bring-up and build steps, see `docs/ANDROID.md`.
