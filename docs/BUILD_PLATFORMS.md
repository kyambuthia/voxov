# Platform Support

This document describes the repository as it exists today, not the long-term target vision.

## Status Matrix

| Platform | Status | Runtime Path | Verification |
| --- | --- | --- | --- |
| Linux desktop | Supported | Shared desktop runtime (`src/game/main.cpp` + `src/engine/*`) | CI build + tests + release bundle validation |
| Windows desktop | Supported for release packaging | Shared desktop runtime | Release packaging and startup smoke test |
| macOS desktop | Goal / unverified | Intended shared desktop runtime | No active CI coverage in this repo |
| Android | Active target, separate runtime | `src/game/android_main.cpp` | Release APK build and artifact validation; no automated device smoke test |
| Web | Preview | `src/game/web_main.cpp` | Buildable, but no CI/runtime parity validation |
| iOS | Scaffold | `src/platform/ios_platform.cpp` | Placeholder only |
| Consoles | Scaffold | `src/platform/console_platform.cpp` | Placeholder only |
| XR | Scaffold | `src/engine_xr/xr_session.cpp` | Placeholder only |

## Desktop

Configure and build:

```bash
cmake -S . -B ./build/desktop/main -DVOXOV_BUILD_TESTS=ON
cmake --build ./build/desktop/main --parallel
```

Run desktop:

```bash
./build/desktop/main/bin/voxov
```

Run local client + server:

```bash
./build/desktop/main/bin/voxov --server
```

Run dedicated server mode:

```bash
./build/desktop/main/bin/voxov --headless-server --port 7777
```

Notes:

- Desktop is now OpenGL-only and is intended to stay inside a GLES3/WebGL2-class rendering budget.
- `--headless-server` is currently a mode of the desktop client executable, not a separate `voxov_server` binary.
- `ctest` requires a build configured with `-DVOXOV_BUILD_TESTS=ON`.

## Android

Android is not just a stub, but it is not yet the same runtime path as desktop.

- Native target: `voxov_android`
- Entry point: `src/game/android_main.cpp`
- Packaging flow: Gradle app under `android/`

Build via Gradle:

```bash
gradle -p android :app:assembleDebug
```

See `docs/ANDROID.md` for NDK and APK details.

## Web

Web is a preview path used for lightweight runtime bring-up, menu flow, and transport-hook experimentation.

Configure:

```bash
EM_CACHE=./build/web/cache emcmake cmake -S . -B ./build/web/main -G Ninja
```

Build:

```bash
EM_CACHE=./build/web/cache cmake --build ./build/web/main --parallel
```

Expected outputs:

- `./build/web/main/bin/voxov_web.html`
- `./build/web/main/bin/voxov_web.js`
- `./build/web/main/bin/voxov_web.wasm`

## Platform Selection Notes

- Android and Web targets are selected by the active toolchain (`ANDROID` or `EMSCRIPTEN`), not by desktop build flags alone.
- The repo still contains target-selection and platform-status docs that describe the long-term vision; use this file plus `docs/ACTIVE_TARGETS.md` as the ground truth for current support levels.
