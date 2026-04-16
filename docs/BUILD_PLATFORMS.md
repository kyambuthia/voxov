# Platform Support

This document describes the repository as it exists today, not the long-term target vision.

## Status Matrix

| Platform | Status | Runtime Path | Verification |
| --- | --- | --- | --- |
| Linux desktop | Supported | Shared desktop runtime (`src/game/main.cpp` + `src/game/game_runtime.cpp`) | CI build + tests + release bundle validation |
| Windows desktop | Supported for release packaging | Shared desktop runtime | Release packaging and startup smoke test |
| macOS desktop | Goal / unverified | Intended shared desktop runtime | No active CI coverage in this repo |
| Android | Supported | `src/game/android_main.cpp` | Release APK validation + CI emulator startup smoke |
| Web | Preview | `src/game/web_main.cpp` | CI Emscripten build + artifact smoke check |
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

Run dedicated server:

```bash
./build/desktop/main/bin/voxov_server --port 7777
```

Notes:

- Desktop is now OpenGL-only and is intended to stay inside a GLES3/WebGL2-class rendering budget.
- `voxov_server` is the preferred standalone authoritative server target.
- `voxov --headless-server` still exists as a compatibility path for quick bring-up from the client executable.
- `ctest` requires a build configured with `-DVOXOV_BUILD_TESTS=ON`.

## Android

Android is not just a stub. It remains a separate runtime path from desktop, but now has CI startup smoke coverage.

- Native target: `voxov_android`
- Entry point: `src/game/android_main.cpp`
- Packaging flow: Gradle app under `android/`

Build via Gradle:

```bash
gradle -p android :app:assembleDebug
```

See `docs/ANDROID.md` for NDK and APK details.

## Web

Web is a preview path used for lightweight runtime bring-up, menu flow, and transport-hook experimentation, with CI build validation.

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
