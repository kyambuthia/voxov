# Android Build Guide

## Current status

Android now has a native build target:

- CMake target: `voxov_android`
- Output library name: `libvoxov.so`
- Entry point: `src/game/android_main.cpp` (`android_main` via `native_app_glue`)

This is a foundation target for bring-up and logging. Full gameplay/render loop integration on Android is the next phase.

## Prerequisites

- Android Studio installed
- Android SDK + NDK installed
- CMake available from Android Studio or system
- Environment variable `ANDROID_NDK_HOME` set (or provide full NDK path manually)

## Build from command line (arm64)

```bash
cmake -S . -B build-android-arm64 \
  -G Ninja \
  -DANDROID=ON \
  -DCMAKE_SYSTEM_NAME=Android \
  -DCMAKE_ANDROID_NDK="$ANDROID_NDK_HOME" \
  -DCMAKE_ANDROID_ARCH_ABI=arm64-v8a \
  -DCMAKE_ANDROID_API=29 \
  -DCMAKE_BUILD_TYPE=Debug

cmake --build build-android-arm64 --parallel
```

Expected output:

- `build-android-arm64/lib/libvoxov.so` (or equivalent library output path)

## Android Studio integration (externalNativeBuild)

1. Create or open an Android app module.
2. Use `externalNativeBuild.cmake` and point to this repo `CMakeLists.txt`.
3. Pass CMake arguments:
   - `-DANDROID=ON`
   - ABI and API level from your Gradle config.
4. Build the app; Gradle will build and package `libvoxov.so`.

## Validate runtime logs

After launch, check Logcat for tag `VOXOV`:

- `android_main started`
- `APP_CMD_INIT_WINDOW`
- `APP_CMD_GAINED_FOCUS`

## Next integration steps

1. Hook Android input/touch/gamepad into engine input pipeline.
2. Add Android Vulkan surface + renderer path through platform abstraction.
3. Wire asset packaging/loading for APK/AAB.
4. Add Android-specific memory/performance profiles for chunk streaming.
