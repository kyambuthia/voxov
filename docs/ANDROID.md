# Android Build Guide

## Current status

Android is an active gameplay target, not just bring-up.

- Runtime entrypoint: `src/game/android_main.cpp`
- Native target: `voxov_android` (`libvoxov.so`)
- Rendering path: EGL + OpenGL ES runtime in native loop
- Multiplayer path: ENet client/server integration, remote interpolation, dev HUD net stats
- LAN discovery: multicast permission + native-side `WifiManager.MulticastLock` management during host/join discovery

## Prerequisites

- Android Studio installed
- Android SDK + NDK installed
- Java 21 (for Gradle release workflow parity)
- `ANDROID_NDK_HOME` set when doing direct native CMake builds

## Build via Gradle (recommended)

From repo root:

```bash
gradle -p android :app:assembleDebug
```

Release APK (signed via configured keystore vars):

```bash
gradle -p android :app:assembleRelease
```

Expected artifacts:

- Debug: `android/app/build/outputs/apk/debug/app-debug.apk`
- Release: `android/app/build/outputs/apk/release/app-release.apk`

## Build native library via CMake (arm64)

```bash
cmake -S . -B ./build/android/arm64-cmake \
  -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=29 \
  -DCMAKE_BUILD_TYPE=Debug

cmake --build ./build/android/arm64-cmake --parallel
```

Expected native output:

- `./build/android/arm64-cmake/lib/libvoxov.so` (or equivalent toolchain output path)

## Runtime validation checklist

After install/launch, check Logcat (`VOXOV` tag):

- `android_main started`
- `APP_CMD_INIT_WINDOW`
- `EGL ready`
- periodic frame/network lines including `rem=`, `cpps=`, `snap=`

Multiplayer checks:

1. Host LAN on one device and join nearby from another.
2. Confirm remote players are visible and animated.
3. Confirm leaving/joining updates remote count and no stale remotes remain.

## Notes

- Android currently uses a native GLES runtime path and does not yet share the full desktop `GameRuntime` orchestration layer (GEA §1.6.15 — cross-platform runtime convergence).
- Ongoing work should prioritize shared multiplayer/simulation helpers over renderer rewrites.
- For the target shared runtime architecture, see `docs/ARCHITECTURE.md` and `docs/ROADMAP.md` Phase 10.
