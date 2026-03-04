# Build Targets

VOXOV targets cross-platform shipping from one architecture:

- Desktop: Linux/Windows/macOS
- Mobile: Android/iOS
- Consoles: PlayStation/Xbox/Nintendo platform targets

All generated outputs must live under `./build/<target>/...`.

## Desktop (Windows/Linux/macOS)

Configure + build:

```bash
cmake -S . -B ./build/desktop/main -DVOXOV_BUILD_TESTS=ON
cmake --build ./build/desktop/main --parallel
```

Run Vulkan backend:

```bash
./build/desktop/main/bin/voxov --renderer vulkan
```

Run OpenGL backend:

```bash
./build/desktop/main/bin/voxov --renderer gl
```

Run headless authoritative server:

```bash
./build/desktop/main/bin/voxov --headless-server
```

LAN replication demo:

```bash
# server host
./build/desktop/main/bin/voxov --headless-server --port 7777

# client 1
./build/desktop/main/bin/voxov --renderer vulkan --connect <SERVER_LAN_IP> --port 7777 --devhud

# client 2
./build/desktop/main/bin/voxov --renderer gl --connect <SERVER_LAN_IP> --port 7777 --devhud
```

## Android

Android has a dedicated native runtime target when `ANDROID=ON`:

- Target: `voxov_android` (shared library)
- Entry point: `src/game/android_main.cpp`
- Packaging/build flow: Gradle app module under `android/`

See `docs/ANDROID.md` for Gradle + native CMake details.

## Web

Web preview target (Emscripten):

```bash
EM_CACHE=./build/web/cache emcmake cmake -S . -B ./build/web/main -G Ninja
EM_CACHE=./build/web/cache cmake --build ./build/web/main --parallel
```

Output:

- `./build/web/main/bin/voxov_web.html`
- `./build/web/main/bin/voxov_web.js`
- `./build/web/main/bin/voxov_web.wasm`

## iOS (planned scaffold)

- `src/platform/ios_platform.cpp` compiles as a lifecycle/event scaffold.
- Full runtime integration is still planned work.

## Consoles (planned scaffold)

- `src/platform/console_platform.cpp` compiles as a placeholder backend.
- Platform certification and memory-profile work is deferred.
