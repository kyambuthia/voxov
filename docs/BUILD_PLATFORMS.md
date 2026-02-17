# Build Targets

VOXOV targets true cross-platform shipping from one architecture:

- Desktop: Linux/Windows/macOS
- Mobile: Android/iOS
- Consoles: PlayStation/Xbox/Nintendo platform targets

## Desktop (Windows/Linux/macOS)

```bash
cmake -S . -B build -DVOXOV_BUILD_TESTS=ON
cmake --build build -j
```

Run Vulkan backend:

```bash
./build/bin/voxov --renderer vulkan
```

Run OpenGL backend:

```bash
./build/bin/voxov --renderer gl
```

Run headless authoritative server:

```bash
./build/bin/voxov --headless-server
```

LAN replication demo (same Wi-Fi):

```bash
# server host
./build/bin/voxov --headless-server --port 7777

# client 1
./build/bin/voxov --renderer vulkan --connect <SERVER_LAN_IP> --port 7777 --devhud

# client 2
./build/bin/voxov --renderer gl --connect <SERVER_LAN_IP> --port 7777 --devhud
```

## Android (foundation)

Skeleton backend compiled: `src/platform/android_platform.cpp`.
Desktop build keeps it as a no-op stub unless `__ANDROID__` is defined.

Suggested next step:
- add `src/platform/android_platform.cpp`
- add `VOXOV_PLATFORM_ANDROID` compile option
- route Vulkan backend window surface creation through platform abstraction.

## iOS (planned)

Requirements:

- platform backend under `platform/*` for iOS lifecycle/input/window integration
- Vulkan strategy via platform-approved graphics path (for example MoltenVK where valid)
- touch/gamepad input mapping through shared input abstraction
- budgeted streaming profile using mobile memory caps

## Consoles (planned)

Requirements:

- no gameplay/render logic forks; platform code contained in backend and platform layers
- fixed memory/streaming budgets per platform profile
- renderer backend capability table to enable/disable optional features safely
- network protocol and save paths remain deterministic and certification-friendly

## Web (foundation)

Skeleton backend compiled: `src/platform/web_platform.cpp`.
Desktop build keeps it as a no-op stub unless `__EMSCRIPTEN__` is defined.

Suggested next step:
- add Emscripten toolchain target in CMake
- implement `web_platform.cpp` with emscripten main loop
- replace GL fixed-function draw path with GLSL ES 3.0 shaders for WebGL2 compatibility.
